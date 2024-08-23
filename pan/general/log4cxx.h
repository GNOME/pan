#ifndef __Log4cxx_h__
#define __Log4cxx_h__

// See https://logging.apache.org/log4cxx/latest_stable/quick-start.html#configuration

#include <log4cxx/basicconfigurator.h>
#include <log4cxx/propertyconfigurator.h>
#include <log4cxx/logmanager.h>

namespace pan {

log4cxx::LoggerPtr getLogger(std::string const &name);

}

#endif /* __Log_4cxx)H__ */
