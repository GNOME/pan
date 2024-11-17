// See https://logging.apache.org/log4cxx/latest_stable/quick-start.html#configuration

#include <gio/gio.h>
#include <glib.h> // for g_build_filename

#include <fstream>
#include <iostream>

#include <log4cxx/logger.h>
#include <pan/general/file-util.h>
#include <pan/general/log4cxx.h>

namespace pan {

log4cxx::LoggerPtr getLogger(std::string const &name)
{
  using namespace log4cxx;

  static struct log4cxx_initializer
  {
      log4cxx_initializer()
      {
        char *filename = g_build_filename(
          file::get_pan_home().c_str(), "pan-log.properties", nullptr);
        GFile *file = g_file_new_for_path((char *)filename);

        if (! g_file_query_exists(file, nullptr))
        {
          // write a default file
          std::ofstream outfile(filename);
          outfile << "# A1 is set to be a ConsoleAppender." << std::endl;
          outfile << "log4j.appender.A1=org.apache.log4j.ConsoleAppender"
                  << std::endl;
          outfile << std::endl;
          outfile << "# A1 uses PatternLayout." << std::endl;
          outfile << "log4j.appender.A1.layout=org.apache.log4j.PatternLayout"
                  << std::endl;
          outfile << "# see "
                     "https://logging.apache.org/log4cxx/latest_stable/"
                     "concepts.html#pattern1"
                  << std::endl;
          outfile
            << "log4j.appender.A1.layout.ConversionPattern=(%F:%C[%M]:%L) %m%n"
            << std::endl;
          outfile << std::endl;
          outfile << "# set to ERROR, WARN, INFO, DEBUG or TRACE" << std::endl;
          outfile << "log4j.logger.Pan=WARN, A1" << std::endl;
          outfile << std::endl;
          outfile
            << "# Other loggers will come and should be listed in README.org"
            << std::endl;
          outfile << "log4j.logger.article=WARN, A1" << std::endl;
          outfile << "log4j.logger.article-tree=WARN, A1" << std::endl;
          outfile << "log4j.logger.decoder=WARN, A1" << std::endl;
          outfile << "log4j.logger.group=WARN, A1" << std::endl;
          outfile << "log4j.logger.group-pane=WARN, A1" << std::endl;
          outfile << "log4j.logger.header=WARN, A1" << std::endl;
          outfile << "log4j.logger.header-tree=WARN, A1" << std::endl;
          outfile << "log4j.logger.nzb=WARN, A1" << std::endl;
          outfile << "log4j.logger.queue=WARN, A1" << std::endl;
          outfile << "log4j.logger.server=WARN, A1" << std::endl;
          outfile << "log4j.logger.task-article=WARN, A1" << std::endl;
          outfile << "log4j.logger.task-xover=WARN, A1" << std::endl;
          outfile << "log4j.logger.xover=WARN, A1" << std::endl;
        }
        if (PropertyConfigurator::configure(filename)
            == spi::ConfigurationStatus::NotConfigured) {
            std::cout << "Error in  log config file\n";
            BasicConfigurator::configure(); // Send events to the console
        }

        g_free(file);
        g_free(filename);
      }

      ~log4cxx_initializer()
      {
        LogManager::shutdown();
      }
  } initAndShutdown;

  return name.empty() ? LogManager::getRootLogger() :
                        LogManager::getLogger(name);
}

} // namespace pan
