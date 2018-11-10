/**@file pickle.hpp
 * @brief A wrapper for the Pickle library <https://github.com/howerj/pickle>,
 * which is derived from an interpreter called 'picol' by Antirez.
 * @license BSD see pickle.c
 * @author Richard James Howe */
#ifndef PICKLE_CPP_H
#define PICKLE_CPP_H

#include "pickle.h"
#include <assert.h>
#include <exception>
#include <new>
#include <string>

class Pickle {
	private:
		pickle_t *p;
	public:
		Pickle(pickle_allocator_t *a = nullptr) {
			p = nullptr;
			const int r = pickle_new(&p, a);
			if (r != PICKLE_OK || p == nullptr)
				throw std::bad_alloc();
		}

		~Pickle() {
			assert(p != NULL);
			const int r = pickle_delete(p);
			assert(r == PICKLE_OK);
			(void)r;
		}

		std::string *eval(const char *s, int &r) {
			assert(s);
			r = pickle_eval(p, s);
			const char *rs = NULL;
			if (pickle_get_result_string(p, &rs) != PICKLE_OK)
				throw;
			assert(rs);
			return new std::string(rs);
		}

		std::string *get(const char *name) {
			assert(name);
			const char *val = NULL;
			const int r = pickle_get_var_string(p, name, &val);
			if (r != PICKLE_OK || val == NULL)
				return nullptr;
			return new std::string(val);
		}

		int set(const char *name, const char *val) {
			assert(name);
			assert(val);
			return pickle_set_var_string(p, name, val);
		}

		std::string *get(const std::string *name)          { assert(name); return get(name->c_str()); }
		std::string *get(const std::string &name)          {               return get(name.c_str());  }
		std::string *eval(const char *s)                   { int r = 0;    return eval(s, r); }
		std::string *eval(const std::string *s)            { assert(s);    return eval(s->c_str()); }
		std::string *eval(const std::string &s)            {               return eval(s.c_str());  }
		std::string *eval(const std::string *s, int &r)    { assert(s);    return eval(s->c_str(), r); }
		std::string *eval(const std::string &s, int &r)    {               return eval(s.c_str(),  r); }
		int set(const char *name,  const std::string *val) { assert(val);  return set(name,          val->c_str()); } 
		int set(const char *name,  const std::string &val) {               return set(name,          val.c_str()); } 
		int set(const std::string *name, const char *val)  { assert(name); return set(name->c_str(), val); } 
		int set(const std::string &name, const char *val)  {               return set(name.c_str(),  val); } 
		int set(const std::string *name, const std::string *val) { assert(name); assert(val); return set(name->c_str(), val->c_str()); } 
		int set(const std::string &name, const std::string &val) {                            return set(name.c_str(),  val.c_str()); } 
};

#endif
