#include "StumblerLogging.h"

PRLogModuleInfo* GetLog()
{
  static PRLogModuleInfo* log = PR_NewLogModule("mozstumbler");
  return log;
}
