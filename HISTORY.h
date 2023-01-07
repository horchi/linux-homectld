/*
 * -----------------------------------
 * Daemon /  Revision History
 * -----------------------------------
 *
 */

#define _VERSION     "0.0.7"
#define VERSION_DATE "07.02.2023"

#ifdef GIT_REV
#  define VERSION _VERSION "-GIT" GIT_REV
#else
#  define VERSION _VERSION
#endif

/*
 * ------------------------------------

2023-01-07: version 0.0.7
   - minor improvements like vdr url

2022-10-24: version 0.0.6
   - minor improvements like vdr url

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
