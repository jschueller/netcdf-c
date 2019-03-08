/* Copyright 2018-2018 University Corporation for Atmospheric
   Research/Unidata. */
/**
 * @file
 * @internal Contains information for creating provenance
 * info and/or displaying provenance info.
 *
 * It has come to pass that we can't guarantee that this information is
 * contained only within netcdf4 files.  As a result, we need
 * to make printing these hidden attributes available to
 * netcdf3 as well.
 *
 *
 * For netcdf4 files, capture state information about the following:
 * 1. Global: netcdf library version
 * 2. Global: hdf5 library version
 * 3. Per file: superblock version
 * 4. Per File: was it created by netcdf-4?
 * 5. Per file: _NCProperties attribute
 *
 * @author Dennis Heimbigner, Ward Fisher
*/

#ifndef _NCPROVENANCE_
#define _NCPROVENANCE_

#define NCPROPS "_NCProperties"
#define NCPVERSION "version" /* Of the properties format */
#define NCPHDF5LIB1 "hdf5libversion"
#define NCPNCLIB1 "netcdflibversion"
#define NCPHDF5LIB2 "hdf5"
#define NCPNCLIB2 "netcdf"
#define NCPROPS_VERSION (2)
/* Version 2 changes this because '|' was causing bash problems */
#define NCPROPSSEP1  '|'
#define NCPROPSSEP2  ','

/* Other hidden attributes */
#define ISNETCDF4ATT "_IsNetcdf4"
#define SUPERBLOCKATT "_SuperblockVersion"

/* Forward */
struct NC_FILE_INFO;

/**************************************************/
/**
   For netcdf4 files, capture state information about the following:
   1. Global: netcdf library version
   2. Global: hdf5 library version
   3. Per file: superblock version
   4. Per File: was it created by netcdf-4?
   5. Per file: _NCProperties attribute
*/

/* Most of this needs to be moved to hdf5internal.h */

#define NCPROPS "_NCProperties"
#define NCPVERSION "version" /* Of the properties format */
#define NCPHDF5LIB1 "hdf5libversion"
#define NCPNCLIB1 "netcdflibversion"
#define NCPHDF5LIB2 "hdf5"
#define NCPNCLIB2 "netcdf"
#define NCPROPS_VERSION (2)
/* Version 2 changes this because '|' was causing bash problems */
#define NCPROPSSEP1  '|'
#define NCPROPSSEP2  ','

/* Other hidden attributes */
#define ISNETCDF4ATT "_IsNetcdf4"
#define SUPERBLOCKATT "_SuperblockVersion"

typedef struct NC4_Provenance {
    int version; /* 0 => not defined */
    char* ncproperty; /* raw value of _NCProperties */
    int superblockversion;
    /* Following is filled from NCPROPS attribute or from global version */
    /* Version 1 format is:
       "netcdflibversion=<version|hdf5libversion=<version>"
       Version 2 format is:
       "<mainbuildlib>=<version|<supportlib1>=<version>...|<other>=..."
    */
    /* The _NCProperties values are stored as an arbitrary set of (key,value) pairs */
    /* It is assumed that the first entry is the primary library
       used to build the file, and it is followed by other libraries
       used in the build, and finally an arbitrary list of other
       (key,value) pairs. */
    NClist* properties;
} NC4_Provenance;

/* Provenance Management (moved from nc4internal.h) */

/* Initialize the provenance global state */
extern int NC4_provenance_init(void);

/* Finalize the provenance global state */
extern int NC4_provenance_finalize(void);

/* Get the NC4_Provenance for a file, parse properties if not already; result is read only */
extern int NC4_get_provenance(struct NC_FILE_INFO* file, const NC4_Provenance**);

/* Read and store the provenance from an existing file */
extern int NC4_read_provenance(struct NC_FILE_INFO* file);

/* Create the provenance for a newly created file */
extern int NC4_new_provenance(struct NC_FILE_INFO* file);

/* Clean up the provenance info in a file */
extern int NC4_clear_provenance(NC4_Provenance* prov);

#endif /* _NCPROVENANCE_ */
