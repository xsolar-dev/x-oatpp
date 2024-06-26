/***************************************************************************
 *
 * Project         _____    __   ____   _      _
 *                (  _  )  /__\ (_  _)_| |_  _| |_
 *                 )(_)(  /(__)\  )( (_   _)(_   _)
 *                (_____)(__)(__)(__)  |_|    |_|
 *
 *
 * Copyright 2018-present, Leonid Stryzhevskyi <lganzzzo@gmail.com>
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

#ifndef oatpp_data_mapping_TypeResolver_hpp
#define oatpp_data_mapping_TypeResolver_hpp

#include "oatpp/Types.hpp"

namespace oatpp { namespace data { namespace mapping {

/**
 * Helper class to work with oatpp types.
 */
class TypeResolver {
public:

  /**
   * Local resolution cache used to reduce number of type interpretation iterations.
   */
  struct Cache {
    /**
     * types map.
     */
    std::unordered_map<const oatpp::Type*, const oatpp::Type*> types;

    /**
     * values by type map.
     */
    std::unordered_map<const oatpp::Type*, std::unordered_map<oatpp::Void, oatpp::Void>> values;
  };

private:

  const oatpp::Type* findPropertyType(const oatpp::Type* baseType,
                                      const std::vector<std::string>& path,
                                      v_uint32 pathPosition,
                                      Cache& cache) const;

  oatpp::Void findPropertyValue(const oatpp::Void& baseObject,
                                const std::vector<std::string>& path,
                                v_uint32 pathPosition,
                                Cache& cache) const;

private:
  std::vector<bool> m_knownClasses;
  std::vector<std::string> m_enabledInterpretations;
public:

  /**
   * Default constructor.
   */
  TypeResolver();

  /**
   * Virtual destructor.
   */
  virtual ~TypeResolver() = default;

  /**
   * Set if the type class is considered known/unknown
   * @param classId
   * @param isKnown
   */
  void setKnownClass(const oatpp::ClassId& classId, bool isKnown);

  /**
   * Set all mentioned type classes as known.
   * @param knownClasses
   */
  void addKnownClasses(const std::vector<oatpp::ClassId>& knownClasses);

  /**
   * Check if type class is known.
   * @param classId
   * @return
   */
  bool isKnownClass(const oatpp::ClassId& classId) const;

  /**
   * Check if type is known.
   * @param type
   * @return
   */
  bool isKnownType(const oatpp::Type* type) const;

  /**
   * Set enabled type interpretations.
   * @param interpretations
   */
  void setEnabledInterpretations(const std::vector<std::string>& interpretations);

  /**
   * Get enabled type interpretations.
   * @return
   */
  const std::vector<std::string>& getEnabledInterpretations() const;

  /**
   * Resolve unknown type according to enabled interpretations.
   * @param type - type to resolve.
   * @param cache - local cache.
   * @return
   */
  const oatpp::Type* resolveType(const oatpp::Type* type, Cache& cache) const;

  /**
   * Resolve unknown value according to enabled interpretations.
   * @param value - value to resolve.
   * @param cache - local cache.
   * @return
   */
  oatpp::Void resolveValue(const oatpp::Void& value, Cache& cache) const;

  /**
   * Traverse object property tree resolving unknown types according to enabled interpretations.
   * @param objectType - base object type.
   * @param path - vector of property names.
   * @param cache - local cache.
   * @return - &id:oatpp::Type;. `nullptr` - if couldn't resolve.
   */
  const oatpp::Type* resolveObjectPropertyType(const oatpp::Type* objectType,
                                              const std::vector<std::string>& path,
                                              Cache& cache) const;

  /**
   * Traverse object property tree resolving unknown value types according to enabled interpretations.
   * @param object - base object.
   * @param path - vector of property names.
   * @param cache - local cache.
   * @return - value as &id:oatpp::Void;. The `valueType` will be set to resolved type
   * or to `oatpp::Void::Class::getType()` if couldn't resolve.
   */
  oatpp::Void resolveObjectPropertyValue(const oatpp::Void& object,
                                         const std::vector<std::string>& path,
                                         Cache& cache) const;

};

}}}

#endif // oatpp_data_mapping_TypeResolver_hpp
