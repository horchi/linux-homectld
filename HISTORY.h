/*
 * -----------------------------------
 * Daemon /  Revision History
 * -----------------------------------
 *
 */

#define _VERSION     "0.0.5"
#define VERSION_DATE "19.07.2022"

#ifdef GIT_REV
#  define VERSION _VERSION "-GIT" GIT_REV
#else
#  define VERSION _VERSION
#endif

/*
 * ------------------------------------

2022-07-19: version 0.0.5
   - git merge

2022-07-19: version 0.0.4
   - development

2022-01-31: version 0.0.3
   - first release of common homed version

2021-05-25: version 0.0.2
   - changes and fixed

2020-04-16: version 0.0.1
   - start of implementation

 * ------------------------------------
 */
