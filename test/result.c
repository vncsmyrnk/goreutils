#include "result.h"

Result result_init(int exit_code) { return (Result){.exit_code = exit_code}; }
