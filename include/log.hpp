#ifndef _LOG_HPP_
#define _LOG_HPP_


#ifdef __cplusplus
extern "C" {
#endif



void LogInit(const char* cpszLogBaseName, long lMaxLogSize, int iMaxLogNum);
void Log(int iLevel, const char* cpszFormat, ...);

#define DEBUG 0
#define ERROR 1

#define Debug(format, ...) \
	Log(DEBUG, "[file %s] [function %s] [line %d]\n" format, __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define Error(format, ...) \
	Log(ERROR, "[file %s] [function %s] [line %d]\n" format, __FILE__, __FUNCTION__, __LINE__, ##__VA_ARGS__)


#ifdef __cplusplus
}
#endif


#endif

