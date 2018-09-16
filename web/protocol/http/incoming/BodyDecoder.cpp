/***************************************************************************
 *
 * Project         _____    __   ____   _      _
 *                (  _  )  /__\ (_  _)_| |_  _| |_
 *                 )(_)(  /(__)\  )( (_   _)(_   _)
 *                (_____)(__)(__)(__)  |_|    |_|
 *
 *
 * Copyright 2018-present, Leonid Stryzhevskyi, <lganzzzo@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************/

#include "BodyDecoder.hpp"

#include "oatpp/core/data/stream/StreamBufferedProxy.hpp"
#include "oatpp/core/utils/ConversionUtils.hpp"

namespace oatpp { namespace web { namespace protocol { namespace http { namespace incoming {

os::io::Library::v_size BodyDecoder::readLine(const std::shared_ptr<oatpp::data::stream::InputStream>& fromStream,
                                              p_char8 buffer,
                                              os::io::Library::v_size maxLineSize) {
  
  v_char8 a;
  os::io::Library::v_size count = 0;
  while (fromStream->read(&a, 1) > 0) {
    if(a != '\r') {
      if(count + 1 > maxLineSize) {
        OATPP_LOGE("BodyDecoder", "Error - too long line");
        return 0;
      }
      buffer[count++] = a;
    } else {
      fromStream->read(&a, 1);
      if(a != '\n'){
        OATPP_LOGE("BodyDecoder", "Warning - invalid line breaker");
      }
      return count; // size of line
    }
  }
  
  return count;
  
}
  
void BodyDecoder::doChunkedDecoding(const std::shared_ptr<oatpp::data::stream::InputStream>& fromStream,
                                    const std::shared_ptr<oatpp::data::stream::OutputStream>& toStream) {
  
  auto buffer = oatpp::data::buffer::IOBuffer::createShared();
  
  v_int32 maxLineSize = 8; // 0xFFFFFFFF 4Gb for chunk
  v_char8 lineBuffer[maxLineSize + 1];
  os::io::Library::v_size countToRead;
  
  do {
    
    auto lineSize = readLine(fromStream, lineBuffer, maxLineSize);
    if(lineSize == 0 || lineSize >= maxLineSize) {
      return; // error reading stream
    }
    lineBuffer[lineSize] = 0;
    countToRead = std::strtol((const char*)lineBuffer, nullptr, 16);
    
    if(countToRead > 0) {
      oatpp::data::stream::transfer(fromStream, toStream, countToRead, buffer->getData(), buffer->getSize());
    }
    
    fromStream->read(lineBuffer, 2); // just skip "\r\n"
    
  } while (countToRead > 0);
  
}
  
void BodyDecoder::decode(const std::shared_ptr<Protocol::Headers>& headers,
                         const std::shared_ptr<oatpp::data::stream::InputStream>& bodyStream,
                         const std::shared_ptr<oatpp::data::stream::OutputStream>& toStream) {
  
  auto transferEncoding = headers->get(Header::TRANSFER_ENCODING, nullptr);
  if(transferEncoding && transferEncoding->equals(Header::Value::TRANSFER_ENCODING_CHUNKED)) {
    doChunkedDecoding(bodyStream, toStream);
  } else {
    oatpp::os::io::Library::v_size contentLength = 0;
    auto contentLengthStr = headers->get(Header::CONTENT_LENGTH, nullptr);
    if(!contentLengthStr) {
      return; // DO NOTHING // it is an empty or invalid body
    } else {
      bool success;
      contentLength = oatpp::utils::conversion::strToInt64(contentLengthStr, success);
      if(!success){
        return; // it is an invalid request/response
      }
      auto buffer = oatpp::data::buffer::IOBuffer::createShared();
      oatpp::data::stream::transfer(bodyStream, toStream, contentLength, buffer->getData(), buffer->getSize());
    }
  }
  
}
  
oatpp::async::Action BodyDecoder::doChunkedDecodingAsync(oatpp::async::AbstractCoroutine* parentCoroutine,
                                                         const oatpp::async::Action& actionOnReturn,
                                                         const std::shared_ptr<oatpp::data::stream::InputStream>& fromStream,
                                                         const std::shared_ptr<oatpp::data::stream::OutputStream>& toStream) {
  
  class ChunkedDecoder : public oatpp::async::Coroutine<ChunkedDecoder> {
  private:
    const v_int32 MAX_LINE_SIZE = 8;
  private:
    std::shared_ptr<oatpp::data::stream::InputStream> m_fromStream;
    std::shared_ptr<oatpp::data::stream::OutputStream> m_toStream;
    std::shared_ptr<oatpp::data::buffer::IOBuffer> m_buffer = oatpp::data::buffer::IOBuffer::createShared();
    v_int32 m_currLineLength;
    v_char8 m_lineChar;
    bool m_lineEnding;
    v_char8 m_lineBuffer [16]; // used max 8
    void* m_skipData;
    os::io::Library::v_size m_skipSize;
    bool m_done = false;
  private:
    void prepareSkipRN() {
      m_skipData = &m_lineBuffer[0];
      m_skipSize = 2;
      m_currLineLength = 0;
      m_lineEnding = false;
    }
  public:
    
    ChunkedDecoder(const std::shared_ptr<oatpp::data::stream::InputStream>& fromStream,
                   const std::shared_ptr<oatpp::data::stream::OutputStream>& toStream)
      : m_fromStream(fromStream)
      , m_toStream(toStream)
    {}
    
    Action act() override {
      m_currLineLength = 0;
      m_lineEnding = false;
      return yieldTo(&ChunkedDecoder::readLineChar);
    }
    
    Action readLineChar() {
      auto res = m_fromStream->read(&m_lineChar, 1);
      if(res == oatpp::data::stream::Errors::ERROR_IO_WAIT_RETRY) {
        return oatpp::async::Action::_WAIT_RETRY;
      } else if(res == oatpp::data::stream::Errors::ERROR_IO_RETRY) {
        return oatpp::async::Action::_REPEAT;
      } else if( res < 0) {
        return error("[BodyDecoder::ChunkedDecoder] Can't read line char");
      }
      return yieldTo(&ChunkedDecoder::onLineCharRead);
    }
    
    Action onLineCharRead() {
      if(!m_lineEnding) {
        if(m_lineChar != '\r') {
          if(m_currLineLength + 1 > MAX_LINE_SIZE){
            return error("[BodyDecoder::ChunkedDecoder] too long line");
          }
          m_lineBuffer[m_currLineLength ++] = m_lineChar;
          return yieldTo(&ChunkedDecoder::readLineChar);
        } else {
          m_lineEnding = true;
          return yieldTo(&ChunkedDecoder::readLineChar);
        }
      } else {
        if(m_lineChar != '\n') {
          OATPP_LOGD("[BodyDecoder::ChunkedDecoder]", "Warning - invalid line breaker")
        }
      }
      if(m_currLineLength == 0) {
        return error("Error reading stream. 0-length line");
      }
      m_lineBuffer[m_currLineLength] = 0;
      return yieldTo(&ChunkedDecoder::onLineRead);
    }
    
    Action onLineRead() {
      os::io::Library::v_size countToRead = std::strtol((const char*) m_lineBuffer, nullptr, 16);
      if(countToRead > 0) {
        prepareSkipRN();
        return oatpp::data::stream::transferAsync(this, yieldTo(&ChunkedDecoder::skipRN), m_fromStream, m_toStream, countToRead, m_buffer);
      }
      m_done = true;
      prepareSkipRN();
      return yieldTo(&ChunkedDecoder::skipRN);
    }
    
    Action skipRN() {
      if(m_done) {
        return oatpp::data::stream::readExactSizeDataAsyncInline(m_fromStream.get(),
                                                                           m_skipData,
                                                                           m_skipSize,
                                                                           finish());
      } else {
        return oatpp::data::stream::readExactSizeDataAsyncInline(m_fromStream.get(),
                                                                           m_skipData,
                                                                           m_skipSize,
                                                                           yieldTo(&ChunkedDecoder::readLineChar));
      }
    }
    
  };
  
  return parentCoroutine->startCoroutine<ChunkedDecoder>(actionOnReturn, fromStream, toStream);
  
}
  
oatpp::async::Action BodyDecoder::decodeAsync(oatpp::async::AbstractCoroutine* parentCoroutine,
                                              const oatpp::async::Action& actionOnReturn,
                                              const std::shared_ptr<Protocol::Headers>& headers,
                                              const std::shared_ptr<oatpp::data::stream::InputStream>& bodyStream,
                                              const std::shared_ptr<oatpp::data::stream::OutputStream>& toStream) {
  auto transferEncoding = headers->get(Header::TRANSFER_ENCODING, nullptr);
  if(transferEncoding && transferEncoding->equals(Header::Value::TRANSFER_ENCODING_CHUNKED)) {
    return doChunkedDecodingAsync(parentCoroutine, actionOnReturn, bodyStream, toStream);
  } else {
    oatpp::os::io::Library::v_size contentLength = 0;
    auto contentLengthStr = headers->get(Header::CONTENT_LENGTH, nullptr);
    if(!contentLengthStr) {
      return actionOnReturn; // DO NOTHING // it is an empty or invalid body
    } else {
      bool success;
      contentLength = oatpp::utils::conversion::strToInt64(contentLengthStr, success);
      if(!success){
        return oatpp::async::Action(oatpp::async::Error("Invalid 'Content-Length' Header"));
      }
      return oatpp::data::stream::transferAsync(parentCoroutine,
                                                actionOnReturn,
                                                bodyStream,
                                                toStream,
                                                contentLength,
                                                oatpp::data::buffer::IOBuffer::createShared());
    }
  }
}

}}}}}
