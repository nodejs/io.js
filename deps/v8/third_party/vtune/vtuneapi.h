#ifndef __VTUNE_API_H__
#define __VTUNE_API_H__

#include <map>
#include <memory>
#include <string>
#include "third_party/ittapi/include/ittnotify.h"

class VTuneDomain {
  /* factory method to create vtune domain */
 public:
  /* create vtune domain, domain_name should be unique, if there
     already has a domain with same name, return false.
   */
  static std::shared_ptr<VTuneDomain> createDomain(const char* domain_name);

  /* destory vtune domain*/
  static void destroyDomain(const char* domain_name);

  /* get a domain */
  static std::shared_ptr<VTuneDomain> getDomain(const char* domain_name);

  /* domain interface */
 public:
  /* start a task with name, if no task name or null, use "default_task_name"*/
  bool beginTask(const char* task_name = "default_task_name");

  /* end a task, if there is no stacked task, return false*/
  void endTask();

 public:
  virtual ~VTuneDomain() {}

 protected:
  static __itt_string_handle* getString(const char* str);

 protected:
  VTuneDomain(__itt_domain* domain) { domain_ = domain; }

 private:
  static std::map<std::string, std::shared_ptr<VTuneDomain>> domains_;
  static std::map<std::string, __itt_string_handle*> string_handlers_;

 private:
  __itt_domain* domain_;
};

#endif
