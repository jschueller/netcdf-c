/**
 * @file
 * @internal Add provenance info for netcdf-4 files.
 *
 * Copyright 2018, UCAR/Unidata See netcdf/COPYRIGHT file for copying
 * and redistribution conditions.
 * @author Dennis Heimbigner
 */

#include "config.h"
#include "nc4internal.h"
#include "hdf5internal.h"
#include "nc_provenance.h"
#include "nclist.h"
#include "ncbytes.h"

/* Provide a hack to suppress the writing of _NCProperties attribute.
   This is for creating a file without _NCProperties for testing purposes.
*/
#undef SUPPRESSNCPROPS

/* Various Constants */
#define NCPROPS_MAX_NAME 1024 /* max key name size */
#define NCPROPS_MAX_VALUE 1024 /* max value size */
#define HDF5_MAX_NAME 1024 /**< HDF5 max name. */

#define ESCAPECHARS "\\=|,"

/** @internal Check NetCDF return code. */
#define NCHECK(expr) {if((expr)!=NC_NOERR) {goto done;}}

/** @internal Check HDF5 return code. */
#define HCHECK(expr) {if((expr)<0) {ncstat = NC_EHDFERR; goto done;}}

static int globalpropinitialized = 0;
static NC4_Provenance globalprovenance;

/* Forward */
static char* locate(char* p, char tag);
static int properties_getversion(const char* propstring, int* versionp);
static int properties_parse(const char* text0, NClist* pairs);
static void escapify(NCbytes* buffer, const char* s);
static int parse_provenance(NC4_Provenance*);
static int build_propstring(int version, NClist* list, char** spropp);

/**
 * @internal Initialize default provenance info
 * This will only be used for newly created files
 * or for opened files that do not contain an _NCProperties
 * attribute.
 *
 * @return ::NC_NOERR No error.
 * @author Dennis Heimbigner
 */
int
NC4_provenance_init(void)
{
    int stat = NC_NOERR;
    int i;
    NClist* other = NULL;
    char* name = NULL;
    char* value = NULL;
    unsigned major,minor,release;
    char* cachedpropstring = NULL;

    if(globalpropinitialized)
        return stat;

    /* Build _NCProperties info */

    /* Initialize globalpropinfo */
    memset((void*)&globalprovenance,0,sizeof(NC4_Provenance));
    globalprovenance.version = NCPROPS_VERSION;
    if((globalprovenance.properties = nclistnew()) == NULL)
        {stat = NC_ENOMEM; goto done;}

    /* Insert primary library version as first entry */
    if((name = strdup(NCPNCLIB2)) == NULL)
    {stat = NC_ENOMEM; goto done;}
    nclistpush(globalprovenance.properties,name);
    name = NULL; /* Avoid multiple free() */

    if((value = strdup(PACKAGE_VERSION)) == NULL)
    {stat = NC_ENOMEM; goto done;}
    nclistpush(globalprovenance.properties,value);
    value = NULL;

    /* Insert the HDF5 as underlying storage format library */
    if((name = strdup(NCPHDF5LIB2)) == NULL)
    {stat = NC_ENOMEM; goto done;}
    nclistpush(globalprovenance.properties,name);
    name = NULL;

    stat = NC4_hdf5get_libversion(&major,&minor,&release);
    if(stat) goto done;
    {
        char sversion[64];
        snprintf(sversion,sizeof(sversion),"%1u.%1u.%1u",major,minor,release);
        if((value = strdup(sversion)) == NULL)
        {stat = NC_ENOMEM; goto done;}
    }
    nclistpush(globalprovenance.properties,value);
    value = NULL;

    /* Add any extra fields */
    /*Parse them into an NClist */
    other = nclistnew();
    if(other == NULL) {stat = NC_ENOMEM; goto done;}
#ifdef NCPROPERTIES
    stat = properties_parse(NCPROPERTIES_EXTRA,other);
    if(stat) goto done;
#endif
    /* merge into the properties list */
    for(i=0;i<nclistlength(other);i++)
        nclistpush(globalprovenance.properties,strdup(nclistget(other,i)));
    nclistfreeall(other);
    other = NULL;

    /* Convert to a string and cache it */
    if((stat = build_propstring(NCPROPS_VERSION,globalprovenance.properties,&cachedpropstring)))
	goto done;
    globalprovenance.ncproperty = cachedpropstring;
    cachedpropstring = NULL;

done:
    if(name != NULL) free(name);
    if(value != NULL) free(value);
    if(cachedpropstring != NULL) free(cachedpropstring);
    if(other != NULL)
        nclistfreeall(other);
    if(stat && globalprovenance.properties != NULL) {
        nclistfreeall(globalprovenance.properties);
        globalprovenance.properties = NULL;
    }
    if(stat == NC_NOERR)
        globalpropinitialized = 1; /* avoid repeating it */
    return stat;
}

/**
 * @internal finalize default provenance info
 *
 * @return ::NC_NOERR No error.
 * @author Dennis Heimbigner
 */
int
NC4_provenance_finalize(void)
{
    return NC4_clear_provenance(&globalprovenance);
}

/**
 * @internal
 *
 * Construct the provenance information for a newly created file.
 * Note that creation of the _NCProperties attribute is deferred
 * to the sync_netcdf4_file function.
 *
 * @param file Pointer to file object.
 *
 * @return ::NC_NOERR No error.
 * [Note: other errors are reported via LOG()]
 * @author Dennis Heimbigner
 */
int
NC4_new_provenance(NC_FILE_INFO_T* file)
{
    int ncstat = NC_NOERR;
    NC4_Provenance* provenance = NULL;
    int superblock = -1;

    LOG((5, "%s: ncid 0x%x", __func__, file->root_grp->hdr.id));

    assert(file->provenance.ncproperty == NULL); /* not yet defined */

    provenance = &file->provenance;
    memset(provenance,0,sizeof(NC4_Provenance)); /* make sure */

    /* Set the version */
    provenance->version = globalprovenance.version;

    /* Set the superblock number */
    if((ncstat = NC4_hdf5get_superblock(file,&superblock))) goto done;
    provenance->superblockversion = superblock;

    if(globalprovenance.ncproperty != NULL) {
        if((provenance->ncproperty = strdup(globalprovenance.ncproperty)) == NULL)
	    {ncstat = NC_ENOMEM; goto done;}
    }

done:
    if(ncstat) {
        LOG((0,"Could not create _NCProperties attribute"));
    }
    return NC_NOERR;
}

/**
 * @internal
 *
 * Construct the provenance information for an existing file.
 *
 * @param file Pointer to file object.
 *
 * @return ::NC_NOERR No error.
 * [Note: other errors are reported via LOG()]
 * @author Dennis Heimbigner
 */
int
NC4_read_provenance(NC_FILE_INFO_T* file)
{
    int ncstat = NC_NOERR;
    NC4_Provenance* provenance = NULL;
    int superblock = -1;
    int version = 0;
    char* propstring = NULL;

    LOG((5, "%s: ncid 0x%x", __func__, file->root_grp->hdr.id));

    assert(file->provenance.version == 0); /* not yet defined */

    provenance = &file->provenance;
    memset(provenance,0,sizeof(NC4_Provenance)); /* make sure */

    /* Set the superblock number */
    if((ncstat = NC4_hdf5get_superblock(file,&superblock))) goto done;
    provenance->superblockversion = superblock;

    /* Read the _NCProperties value from the file */
    if((ncstat = NC4_read_ncproperties(file,&propstring))) goto done;
    provenance->ncproperty = propstring;
    propstring = NULL;    

    /* Get only the version */
    if((ncstat=properties_getversion(propstring,&version)))
        goto done;
    /* save version, even if it is unknown */
    provenance->version = version;

done:
    nullfree(propstring);
    if(ncstat) {
        LOG((0,"Could not create _NCProperties attribute"));
    }
    return NC_NOERR;
}

/* HDF5 Specific attribute read/write of _NCProperties */
int
NC4_read_ncproperties(NC_FILE_INFO_T* h5, char** propstring)
{
    int retval = NC_NOERR;
    hid_t hdf5grpid = -1;
    hid_t attid = -1;
    hid_t aspace = -1;
    hid_t atype = -1;
    hid_t ntype = -1;
    char* text = NULL;
    H5T_class_t t_class;
    hsize_t size;

    LOG((5, "%s", __func__));

    hdf5grpid = ((NC_HDF5_GRP_INFO_T *)(h5->root_grp->format_grp_info))->hdf_grpid;

    if(H5Aexists(hdf5grpid,NCPROPS) <= 0) { /* Does not exist */
        /* File did not contain a _NCProperties attribute; leave empty */
        goto done;
    }

    /* NCPROPS Attribute exists, make sure it is legitimate */
    attid = H5Aopen_name(hdf5grpid, NCPROPS);
    assert(attid > 0);
    aspace = H5Aget_space(attid);
    atype = H5Aget_type(attid);
    /* Verify atype and size */
    t_class = H5Tget_class(atype);
    if(t_class != H5T_STRING)
    {retval = NC_EINVAL; goto done;}
    size = H5Tget_size(atype);
    if(size == 0)
    {retval = NC_EINVAL; goto done;}
    text = (char*)malloc(1+(size_t)size);
    if(text == NULL)
    {retval = NC_ENOMEM; goto done;}
    if((ntype = H5Tget_native_type(atype, H5T_DIR_DEFAULT)) < 0)
    {retval = NC_EHDFERR; goto done;}
    if((H5Aread(attid, ntype, text)) < 0)
    {retval = NC_EHDFERR; goto done;}
    /* Make sure its null terminated */
    text[(size_t)size] = '\0';
    if(propstring) {*propstring = text; text = NULL;}

done:
    if(text != NULL) free(text);
    /* Close out the HDF5 objects */
    if(attid > 0 && H5Aclose(attid) < 0) retval = NC_EHDFERR;
    if(aspace > 0 && H5Sclose(aspace) < 0) retval = NC_EHDFERR;
    if(atype > 0 && H5Tclose(atype) < 0) retval = NC_EHDFERR;
    if(ntype > 0 && H5Tclose(ntype) < 0) retval = NC_EHDFERR;

    /* For certain errors, actually fail, else log that attribute was invalid and ignore */
    if(retval != NC_NOERR) {
        if(retval != NC_ENOMEM && retval != NC_EHDFERR) {
            LOG((0,"Invalid _NCProperties attribute: ignored"));
            retval = NC_NOERR;
        }
    }
    return retval;
}

int
NC4_write_ncproperties(NC_FILE_INFO_T* h5)
{
#ifdef SUPPRESSNCPROPERTY
    return NC_NOERR;
#else /*!SUPPRESSNCPROPERTY*/
    int retval = NC_NOERR;
    hid_t hdf5grpid = -1;
    hid_t attid = -1;
    hid_t aspace = -1;
    hid_t atype = -1;
    size_t len = 0;
    NC4_Provenance* prov = &h5->provenance;

    LOG((5, "%s", __func__));

    /* If the file is read-only, return an error. */
    if (h5->no_write)
    {retval = NC_EPERM; goto done;}

    hdf5grpid = ((NC_HDF5_GRP_INFO_T *)(h5->root_grp->format_grp_info))->hdf_grpid;

    if(H5Aexists(hdf5grpid,NCPROPS) > 0) /* Already exists, no overwrite */
        goto done;

    /* Build the property if we have legit value */
    if(prov->version > 0 && prov->ncproperty != NULL) {
	/* Build the HDF5 string type */
	if ((atype = H5Tcopy(H5T_C_S1)) < 0)
	    {retval = NC_EHDFERR; goto done;}
	if (H5Tset_strpad(atype, H5T_STR_NULLTERM) < 0)
	    {retval = NC_EHDFERR; goto done;}
	if(H5Tset_cset(atype, H5T_CSET_ASCII) < 0)
	    {retval = NC_EHDFERR; goto done;}
	len = strlen(prov->ncproperty);
	if(H5Tset_size(atype, len) < 0)
	    {retval = NC_EFILEMETA; goto done;}
	/* Create NCPROPS attribute */
	if((aspace = H5Screate(H5S_SCALAR)) < 0)
	    {retval = NC_EFILEMETA; goto done;}
	if ((attid = H5Acreate(hdf5grpid, NCPROPS, atype, aspace, H5P_DEFAULT)) < 0)
	    {retval = NC_EFILEMETA; goto done;}
	if (H5Awrite(attid, atype, prov->ncproperty) < 0)
	    {retval = NC_EFILEMETA; goto done;}
/* Verify */
#if 0
    {
        hid_t spacev, typev;
        hsize_t dsize, tsize;
        typev = H5Aget_type(attid);
        spacev = H5Aget_space(attid);
        dsize = H5Aget_storage_size(attid);
        tsize = H5Tget_size(typev);
        fprintf(stderr,"dsize=%lu tsize=%lu\n",(unsigned long)dsize,(unsigned long)tsize);
    }
#endif
    }

done:
    /* Close out the HDF5 objects */
    if(attid > 0 && H5Aclose(attid) < 0) retval = NC_EHDFERR;
    if(aspace > 0 && H5Sclose(aspace) < 0) retval = NC_EHDFERR;
    if(atype > 0 && H5Tclose(atype) < 0) retval = NC_EHDFERR;

    /* For certain errors, actually fail, else log that attribute was invalid and ignore */
    switch (retval) {
    case NC_ENOMEM:
    case NC_EHDFERR:
    case NC_EPERM:
    case NC_EFILEMETA:
    case NC_NOERR:
        break;
    default:
        LOG((0,"Invalid _NCProperties attribute"));
        retval = NC_NOERR;
        break;
    }
    return retval;
#endif /*!SUPPRESSNCPROPERTY*/
}

int
NC4_get_provenance(NC_FILE_INFO_T* file, const NC4_Provenance** provp)
{
    int ncstat = NC_NOERR;
    NC4_Provenance* prov = &file->provenance;

    /* Parse the ncproperty value */	
    if(prov->version > 0 && prov->properties == NULL) {
	if((ncstat = parse_provenance(prov)))
	    goto done;
    }

    if(provp != NULL) *provp = prov;

done:
    return ncstat;
}

/**************************************************/
/* Utilities */

/* Debugging */

void
ncprintprovenance(NC4_Provenance* info)
{
    int i;
    fprintf(stderr,"[%p] version=%d superblockversion=%d ncproperty=|%s|\n",
	info,
	info->version,
	info->superblockversion,
	(info->ncproperty==NULL?"":info->ncproperty));
    for(i=0;i<nclistlength(info->properties);i+=2) {
        char* name = nclistget(info->properties,i);
        char* value = nclistget(info->properties,i+1);
        fprintf(stderr,"\t[%d] name=|%s| value=|%s|\n",i,name,value);
    }
}

/* Locate a specific character and return its pointer
   or EOS if not found
   take \ escapes into account */
static char*
locate(char* p, char tag)
{
    char* next;
    int c;
    assert(p != NULL);
    for(next = p;(c = *next);next++) {
        if(c == tag)
            return next;
        else if(c == '\\' && next[1] != '\0')
            next++; /* skip escaped char */
    }
    return next; /* not found */
}

static int
properties_getversion(const char* propstring, int* versionp)
{
    int ncstat = NC_NOERR;
    int version = 0;
    /* propstring should begin with "version=dddd" */
    if(propstring == NULL || strlen(propstring) < strlen("version=") + strlen("1"))
        {ncstat = NC_EINVAL; goto done;} /* illegal version */	
    if(memcmp(propstring,"version=",strlen("version=")) != 0)
        {ncstat = NC_EINVAL; goto done;} /* illegal version */	
    propstring += strlen("version=");
    /* get version */
    version = atoi(propstring);
    if(version < 0)
        {ncstat = NC_EINVAL; goto done;} /* illegal version */	
    if(versionp) *versionp = version;
done:
    return ncstat;
}

/**
 * @internal Parse file properties.
 *
 * @param text0 Text properties.
 * @param pairs list of parsed (key,value) pairs
 *
 * @return ::NC_NOERR No error.
 * @author Dennis Heimbigner
 */
static int
properties_parse(const char* text0, NClist* pairs)
{
    int ret = NC_NOERR;
    char* p;
    char* q;
    char* text = NULL;

    if(text0 == NULL || strlen(text0) == 0)
        goto done;

    text = strdup(text0);
    if(text == NULL) return NC_ENOMEM;

    /* For back compatibility with version 1, translate '|' -> ',' */
    for(p=text;*p;p++) {
        if(*p == NCPROPSSEP1)
            *p = NCPROPSSEP2;
    }

    /* Walk and fill in ncinfo */
    p = text;
    while(*p) {
        char* name = p;
        char* value = NULL;
        char* next = NULL;

        /* Delimit whole (key,value) pair */
        q = locate(p,NCPROPSSEP2);
        if(*q != '\0') /* Never go beyond the final nul term */
            *q++ = '\0';
        next = q;
        /* split key and value */
        q = locate(p,'=');
        name = p;
        *q++ = '\0';
        value = q;
        /* Set up p for next iteration */
        p = next;
        nclistpush(pairs,strdup(name));
        nclistpush(pairs,strdup(value));
    }
done:
    if(text) free(text);
    return ret;
}


/* Utility to transfer a string to a buffer with escaping */
static void
escapify(NCbytes* buffer, const char* s)
{
    const char* p;
    for(p=s;*p;p++) {
        if(strchr(ESCAPECHARS,*p) != NULL)
            ncbytesappend(buffer,'\\');
        ncbytesappend(buffer,*p);
    }
}

/**
 * @internal
 *
 * Construct the parsed provenance information 
 *
 * @param prov Pointer to provenance object
 *
 * @return ::NC_NOERR No error.
 * @return ::NC_ENOMEM
 * @return ::NC_EINVAL
 * @author Dennis Heimbigner
 */
static int
parse_provenance(NC4_Provenance* prov)
{
    int ncstat = NC_NOERR;
    char *name = NULL;
    char *value = NULL;
    int version = 0;
    NClist* list = NULL;

    LOG((5, "%s: prov 0x%x", __func__, prov));

    if(prov->ncproperty == NULL || strlen(prov->ncproperty) < strlen("version="))
        {ncstat = NC_EINVAL; goto done;}
    if((list = nclistnew()) == NULL)
        {ncstat = NC_ENOMEM; goto done;}

    /* Do we understand the version? */
    if(prov->version > 0 && prov->version <= NCPROPS_VERSION) {/* recognized version */
        if((ncstat=properties_parse(prov->ncproperty,list)))
	    goto done;
        /* Remove version pair from properties list*/
        if(nclistlength(list) < 2)
            {ncstat = NC_EINVAL; goto done;} /* bad _NCProperties attribute */
        /* Throw away the purported version=... */
        nclistremove(list,0); /* version key */
        nclistremove(list,0); /* version value */

        /* Now, rebuild to the latest version */
	switch (version) {
	default: break; /* do nothing */
	case 1: {
            int i;
            for(i=0;i<nclistlength(list);i+=2) {
                char* newname = NULL;
                name = nclistget(list,i);
                if(name == NULL) continue; /* ignore */
                if(strcmp(name,NCPNCLIB1) == 0)
                    newname = NCPNCLIB2; /* change name */
                else if(strcmp(name,NCPHDF5LIB1) == 0)
                    newname = NCPHDF5LIB2;
                else continue; /* ignore */
                /* Do any rename */
                nclistset(list,i,strdup(newname));
                if(name) {free(name); name = NULL;}
            }
        } break;
	} /*switch*/
    }
    prov->properties = list;
    list = NULL;

done:
    nclistfreeall(list);
    if(name != NULL) free(name);
    if(value != NULL) free(value);
    return ncstat;
}

/**
 * @internal Build _NCProperties attribute value.
 *
 * Convert a NCPROPINFO instance to a single string.
 * Will always convert to current format
 *
 * @param version
 * @param list Properties list
 * @param spropp Pointer that gets properties string.
 * @return ::NC_NOERR No error.
 * @return ::NC_EINVAL failed.
 * @author Dennis Heimbigner
 */
static int
build_propstring(int version, NClist* list, char** spropp)
{
    int stat = NC_NOERR;
    int i;
    NCbytes* buffer = NULL;
    char sversion[64];

    LOG((5, "%s version=%d", __func__, version));

    if(spropp != NULL) *spropp = NULL;

    if(version == 0 || version > NCPROPS_VERSION) /* unknown case */
	goto done;
     if(list == NULL)
        {stat = NC_EINVAL; goto done;}

    if((buffer = ncbytesnew()) ==  NULL)
        {stat = NC_ENOMEM; goto done;}

    /* start with version */
    ncbytescat(buffer,NCPVERSION);
    ncbytesappend(buffer,'=');
    /* Use current version */
    snprintf(sversion,sizeof(sversion),"%d",NCPROPS_VERSION);
    ncbytescat(buffer,sversion);

    for(i=0;i<nclistlength(list);i+=2) {
        char* value, *name;
        name = nclistget(list,i);
        if(name == NULL) continue;
        value = nclistget(list,i+1);
        ncbytesappend(buffer,NCPROPSSEP2); /* terminate last entry */
        escapify(buffer,name);
        ncbytesappend(buffer,'=');
        escapify(buffer,value);
    }
    /* Force null termination */
    ncbytesnull(buffer);
    if(spropp) *spropp = ncbytesextract(buffer);

done:
    if(buffer != NULL) ncbytesfree(buffer);
    return stat;
}

/**
 * @internal
 *
 * Clear the NCPROVENANCE object; do not free it
 * @param prov Pointer to provenance object
 *
 * @return ::NC_NOERR No error.
 * @author Dennis Heimbigner
 */
int
NC4_clear_provenance(NC4_Provenance* prov)
{
    LOG((5, "%s", __func__));

    if(prov == NULL) return NC_NOERR;
    nullfree(prov->ncproperty);
    if(prov->properties != NULL)
        nclistfreeall(prov->properties);
    memset(prov,0,sizeof(NC4_Provenance));
    return NC_NOERR;
}

#if 0
/* Unused functions */

/**
 * @internal
 *
 * Clear and Free the NC4_Provenance object
 * @param prov Pointer to provenance object
 *
 * @return ::NC_NOERR No error.
 * @author Dennis Heimbigner
 */
static int
NC4_free_provenance(NC4_Provenance* prov)
{
    LOG((5, "%s", __func__));

    if(prov == NULL) return NC_NOERR;
    NC4_clear_provenance(prov);
    free(prov);
    return NC_NOERR;
}

/* Utility to copy contents of the dfalt into an NCPROPINFO object */
static int
propinfo_default(NC4_Properties* dst, const NC4_Properties* dfalt)
{
    int i;
    if(dst->properties == NULL) {
        dst->properties = nclistnew();
        if(dst->properties == NULL) return NC_ENOMEM;
    }
    dst->version = dfalt->version;
    for(i=0;i<nclistlength(dfalt->properties);i++) {
        char* s = nclistget(dfalt->properties,i);
        s = strdup(s);
        if(s == NULL) return NC_ENOMEM;
        nclistpush(dst->properties,s);
    }
    return NC_NOERR;
}

#endif /*0*/
