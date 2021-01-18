/*
  +----------------------------------------------------------------------+
  | Copyright (c) 2009 The PHP Group                                     |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.0 of the PHP license,       |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_0.txt.                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Authors: Andrei Zmievski <andrei@php.net>                            |
  +----------------------------------------------------------------------+
 */

/* $ Id: $ */

#include "php_bloomy.h"
#include "bloom.h"

#include "ext/standard/php_lcg.h"
#include "ext/standard/php_rand.h"
#include "ext/standard/php_smart_string.h"
#include "Zend/zend_smart_str.h"
#include "ext/standard/php_var.h"



/****************************************
  Structures and definitions
 ****************************************/

#ifdef ZEND_ENGINE_3

typedef struct
{
    bloom_t *bloom;
    zend_object zo;
} php_bloom_t;

#else

typedef struct
{
    zend_object zo;
    bloom_t *bloom;
} php_bloom_t;

#endif

static zend_object_handlers bloom_object_handlers;
static zend_class_entry *bloom_ce = NULL;
static const double DEFAULT_ERROR_RATE = 0.01;


/****************************************
  Forward declarations
 ****************************************/

static void php_bloom_destroy(php_bloom_t *obj TSRMLS_DC);

#ifdef ZEND_ENGINE_3 

static inline php_bloom_t *php_bloom_fetch_object(zend_object *obj)
{
    return (php_bloom_t *) ((char*) (obj) - XtOffsetOf(php_bloom_t, zo));
}
#define Z_BLOOM_P(zv) php_bloom_fetch_object(Z_OBJ_P((zv)))
#else 
#define php_bloom_fetch_object(object) ((php_bloom_t *)object)
#define Z_BLOOM_P(zv) (php_bloom_t *)zend_object_store_get_object(zv TSRMLS_CC)
#endif


/****************************************
  Helper macros
 ****************************************/

#define BLOOM_METHOD_INIT_VARS             \
    zval*             object  = getThis(); \
    php_bloom_t*      obj     = NULL;      \

#define BLOOM_METHOD_FETCH_OBJECT                                             \
        obj = Z_BLOOM_P(object);   \
        if (!obj->bloom) { \
                php_error_docref(NULL TSRMLS_CC, E_WARNING, "BloomFilter constructor was not called"); \
                return; \
        }


#ifdef ZEND_ENGINE_3
#define BLOOM_LEN_TYPE size_t
#else
#define BLOOM_LEN_TYPE int
#endif

#ifdef ZEND_ENGINE_3
#define BLOOM_ZEND_OBJECT zend_object
#else 
#define BLOOM_ZEND_OBJECT php_bloom_t
#endif



/****************************************
  Method implementations
 ****************************************/

/* {{{ BloomFilter::__construct(int capacity [, double error_rate [, int random_seed ] ])
   Creates a new filter with the specified capacity */
static PHP_METHOD(BloomFilter, __construct)
{
    zval *object = getThis();
    php_bloom_t *obj;
    long capacity, seed = 0;
    double error_rate = DEFAULT_ERROR_RATE;
    bloom_return status;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "l|dl", &capacity, &error_rate, &seed) == FAILURE)
    {
        ZVAL_NULL(object);
        return;
    }

    if (capacity == 0 ||
            capacity > SIZE_MAX ||
            error_rate <= 0.0 ||
            error_rate >= 1.0)
    {

        ZVAL_NULL(object);
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "bad filter parameters");
        return;
    }

    if (seed == 0)
    {
        seed = GENERATE_SEED();
    }
    srand(seed);

    obj = Z_BLOOM_P(object);

    obj->bloom = (bloom_t *) emalloc(sizeof (bloom_t));
    status = bloom_init(obj->bloom, capacity, error_rate);

    if (status != BLOOM_SUCCESS)
    {
        //need to throw exception
        ZVAL_NULL(object);
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "could not create filter");
        return;
    }

}
static PHP_METHOD(BloomFilter, read)
{
    char *data = NULL;
    BLOOM_LEN_TYPE data_len;
    BLOOM_METHOD_INIT_VARS;
    bloom_return status;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &data, &data_len) == FAILURE)
    {
        return;
    }

    BLOOM_METHOD_FETCH_OBJECT;

    obj->bloom->salt1 = (*(uint32_t*) data);
    data += sizeof (uint32_t);
    obj->bloom->salt2 = (*(uint32_t*) data);
    data += sizeof (uint32_t);
    obj->bloom->num_elements = (*(size_t*) data);
    data += sizeof (size_t);

    memcpy(obj->bloom->filter, data, obj->bloom->spec.size_bytes);

    RETURN_TRUE;
}

static PHP_METHOD(BloomFilter, write)
{
    BLOOM_METHOD_INIT_VARS;

    BLOOM_METHOD_FETCH_OBJECT;

    size_t len = sizeof (obj->bloom->salt1) + sizeof (obj->bloom->salt2) + sizeof (obj->bloom->num_elements) + obj->bloom->spec.size_bytes;
    
    char *buf = (char *) malloc(len);
    char *start = buf;
    
    (*(uint32_t*) buf) = obj->bloom->salt1;
    buf += sizeof (uint32_t);
    (*(uint32_t*) buf) = obj->bloom->salt2;
    buf += sizeof (uint32_t);
    (*(size_t*) buf) = obj->bloom->num_elements;
    buf += sizeof (size_t);
    
    memcpy(buf, obj->bloom->filter, obj->bloom->spec.size_bytes);

    RETURN_STRINGL(start, len);
    
    free(start);
}

/* }}} */

/* {{{ BloomFilter::add(string item)
   Adds an item to the filter */
static PHP_METHOD(BloomFilter, add)
{
    char *data = NULL;
    BLOOM_LEN_TYPE data_len;
    BLOOM_METHOD_INIT_VARS;
    bloom_return status;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &data, &data_len) == FAILURE)
    {
        return;
    }

    BLOOM_METHOD_FETCH_OBJECT;

    status = bloom_add(obj->bloom, data, data_len);
    if (status != BLOOM_SUCCESS)
    {
        php_error_docref(NULL TSRMLS_CC, E_WARNING, "could not add data to filter");
        RETURN_FALSE;
    }

    RETURN_TRUE;
}
/* }}} */

/* {{{ BloomFilter::has(string item)
   Checks if the filter has the specified item */
static PHP_METHOD(BloomFilter, has)
{
    char *data = NULL;
    BLOOM_LEN_TYPE data_len;
    BLOOM_METHOD_INIT_VARS;
    bloom_return status;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &data, &data_len) == FAILURE)
    {
        return;
    }

    BLOOM_METHOD_FETCH_OBJECT;

    status = bloom_contains(obj->bloom, data, data_len);

    if (status == BLOOM_NOTFOUND)
    {
        RETURN_FALSE;
    }
    else
    {
        RETURN_TRUE;
    }
}
/* }}} */

/* {{{ BloomFilter::getInfo()
   Returns array with filter information */
static PHP_METHOD(BloomFilter, getInfo)
{
    BLOOM_METHOD_INIT_VARS;

    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "") == FAILURE)
    {
        return;
    }

    BLOOM_METHOD_FETCH_OBJECT;

    array_init(return_value);
    add_assoc_double_ex(return_value, ZEND_STRS("error_rate"), obj->bloom->max_error_rate);
    add_assoc_long_ex(return_value, ZEND_STRS("num_hashes"), obj->bloom->spec.num_hashes);
    add_assoc_long_ex(return_value, ZEND_STRS("filter_size"), obj->bloom->spec.filter_size);
    add_assoc_long_ex(return_value, ZEND_STRS("filter_size_in_bytes"), obj->bloom->spec.size_bytes);
    add_assoc_long_ex(return_value, ZEND_STRS("num_items"), obj->bloom->num_elements);
}
/* }}} */


/****************************************
  Internal support code
 ****************************************/

/* {{{ constructor/destructor */
static void php_bloom_destroy(php_bloom_t *obj TSRMLS_DC)
{
    if (obj->bloom)
    {
        bloom_clean(obj->bloom);
        efree(obj->bloom);
    }
}

//static void php_bloom_free_storage(php_bloom_t *obj TSRMLS_DC)
#ifdef ZEND_ENGINE_3
static void php_bloom_free_storage(zend_object *object TSRMLS_DC)
#else 

static void php_bloom_free_storage(php_bloom_t *object TSRMLS_DC)
#endif
{
    php_bloom_t *obj = php_bloom_fetch_object(object);
    zend_object_std_dtor(&obj->zo TSRMLS_CC);
    php_bloom_destroy(obj TSRMLS_CC);

#ifndef ZEND_ENGINE_3
    efree(obj);
#endif
}

#ifdef ZEND_ENGINE_3
zend_object *php_bloom_new(zend_class_entry *ce)
#else

zend_object_value php_bloom_new(zend_class_entry *ce TSRMLS_DC)
#endif
{
    php_bloom_t *obj;
    zval *tmp;

#ifdef ZEND_ENGINE_3
    obj = ecalloc(1,
            sizeof (php_bloom_t) +
            sizeof (zval) * (ce->default_properties_count - 1));
#else
    zend_object_value retval;

    obj = (php_bloom_t *) emalloc(sizeof (*obj));
    memset(obj, 0, sizeof (*obj));
#endif

    zend_object_std_init(&obj->zo, ce TSRMLS_CC);
#if PHP_VERSION_ID < 50399
    zend_hash_copy(obj->zo.properties, &ce->default_properties, (copy_ctor_func_t) zval_add_ref, (void *) &tmp, sizeof (zval *));
#else
    object_properties_init(&(obj->zo), ce);
#endif

#ifdef ZEND_ENGINE_3
    obj->zo.handlers = &bloom_object_handlers;
    return &obj->zo;
#else
    retval.handle = zend_objects_store_put(obj, (zend_objects_store_dtor_t) zend_objects_destroy_object, (zend_objects_free_object_storage_t) php_bloom_free_storage, NULL TSRMLS_CC);
    retval.handlers = zend_get_std_object_handlers();
    return retval;
#endif

}


/* {{{ methods arginfo */
ZEND_BEGIN_ARG_INFO_EX(arginfo___construct, 0, 0, 1)
ZEND_ARG_INFO(0, capacity)
ZEND_ARG_INFO(0, error_rate)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_add, 0)
ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_has, 0)
ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_getInfo, 0)
ZEND_END_ARG_INFO()
/* }}} */

/* {{{ bloom_class_methods */
static zend_function_entry bloom_class_methods[] = {
    PHP_ME(BloomFilter, __construct, arginfo___construct, ZEND_ACC_PUBLIC)
    PHP_ME(BloomFilter, add, arginfo_add, ZEND_ACC_PUBLIC)
    PHP_ME(BloomFilter, has, arginfo_has, ZEND_ACC_PUBLIC)
    PHP_ME(BloomFilter, read, NULL, ZEND_ACC_PUBLIC)
    PHP_ME(BloomFilter, write, NULL, ZEND_ACC_PUBLIC)

    PHP_ME(BloomFilter, getInfo, arginfo_getInfo, ZEND_ACC_PUBLIC)
    { NULL, NULL, NULL}
};
/* }}} */

/* {{{ bloomy_module_entry
 */
zend_module_entry bloomy_module_entry = {
    STANDARD_MODULE_HEADER,
    "bloomy",
    NULL,
    PHP_MINIT(bloomy),
    NULL,
    NULL,
    NULL,
    PHP_MINFO(bloomy),
    PHP_BLOOMY_VERSION,
    STANDARD_MODULE_PROPERTIES
};
/* }}} */

/* {{{ PHP_MINIT_FUNCTION */
PHP_MINIT_FUNCTION(bloomy)
{
    zend_class_entry ce;

    INIT_CLASS_ENTRY(ce, "BloomFilter", bloom_class_methods);
    bloom_ce = zend_register_internal_class(&ce TSRMLS_CC);
    bloom_ce->create_object = php_bloom_new;

    memcpy(&bloom_object_handlers, zend_get_std_object_handlers(), sizeof (zend_object_handlers));

#ifdef ZEND_ENGINE_3
    bloom_object_handlers.offset = XtOffsetOf(php_bloom_t, zo);
    bloom_object_handlers.free_obj = php_bloom_free_storage;
#endif

    return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION */
PHP_MINFO_FUNCTION(bloomy)
{
    php_info_print_table_start();
    php_info_print_table_header(2, "bloomy support", "enabled");
    php_info_print_table_row(2, "Version", PHP_BLOOMY_VERSION);
    php_info_print_table_end();
}
/* }}} */

#ifdef COMPILE_DL_BLOOMY
ZEND_GET_MODULE(bloomy)
#endif

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
