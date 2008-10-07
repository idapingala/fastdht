#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <fcntl.h>
#include <errno.h>
#include <db.h>
#include "logger.h"
#include "shared_func.h"
#include "db_op.h"

static void db_errcall(const DB_ENV *dbenv, const char *errpfx, const char *msg)
{
	logError("file: "__FILE__", line: %d, " \
		"db error, error info: %s", \
		__LINE__, msg);
}

int db_init(DBInfo *pDBInfo, const DBType type, const u_int64_t nCacheSize, \
	const char *base_path, const char *filename)
{
#define _DB_BLOCK_BYTES   (256 * 1024 * 1024)
	int result;
	u_int32_t gb;
	u_int32_t bytes;
	int blocks;
	char *sub_dirs[] = {"tmp", "logs", "data"};
	char full_path[256];
	int i;

	pDBInfo->env = NULL;
	pDBInfo->db = NULL;
	
	for (i=0; i<sizeof(sub_dirs)/sizeof(char *); i++)
	{
		snprintf(full_path, sizeof(full_path), "%s/%s", \
			base_path, sub_dirs[i]);
		if (!fileExists(full_path))
		{
			if (mkdir(full_path, 0755) != 0)
			{
				logError("file: "__FILE__", line: %d, " \
					"mkdir %s fail, " \
					"errno: %d, error info: %s", \
					__LINE__, full_path, \
					errno, strerror(errno));
				return errno != 0 ? errno : EPERM;
			}
		}
	}

	if ((result=db_env_create(&(pDBInfo->env), 0)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"db_env_create fail, errno: %d, error info: %s", \
			__LINE__, result, db_strerror(result));
		return result;
	}

	if ((result=pDBInfo->env->set_alloc(pDBInfo->env, \
			malloc, realloc, free)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"env->set_alloc fail, errno: %d, error info: %s", \
			__LINE__, result, db_strerror(result));
		return result;
	}

	pDBInfo->env->set_tmp_dir(pDBInfo->env, "tmp");
	pDBInfo->env->set_lg_dir(pDBInfo->env, "logs");
	pDBInfo->env->set_data_dir(pDBInfo->env, "data");
	pDBInfo->env->set_errcall(pDBInfo->env, db_errcall);

	gb = (u_int32_t)(nCacheSize / (1024 * 1024 * 1024));
	bytes = (u_int32_t)(nCacheSize - (u_int64_t)gb  * (1024 * 1024 * 1024));
	blocks = (int)(nCacheSize / _DB_BLOCK_BYTES);
	if (nCacheSize % _DB_BLOCK_BYTES != 0)
	{
		blocks++;
	}
	if ((result=pDBInfo->env->set_cachesize(pDBInfo->env, \
			gb, bytes, blocks)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"env->set_cachesize fail, errno: %d, error info: %s", \
			__LINE__, result, db_strerror(result));
		return result;
	}

	if ((result=pDBInfo->env->open(pDBInfo->env, base_path, \
		DB_CREATE | DB_INIT_MPOOL | DB_INIT_LOG | DB_THREAD, 0644)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"env->open fail, errno: %d, error info: %s", \
			__LINE__, result, db_strerror(result));
		return result;
	}

	if ((result=db_create(&(pDBInfo->db), pDBInfo->env, 0)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"db_create fail, errno: %d, error info: %s", \
			__LINE__, result, db_strerror(result));
		return result;
	}

	if ((result=pDBInfo->db->open(pDBInfo->db, NULL, filename, NULL, \
		type, DB_CREATE | DB_THREAD, 0644)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"db->open fail, errno: %d, error info: %s", \
			__LINE__, result, db_strerror(result));
		return result;
	}

	return 0;
}

int db_destroy(DBInfo *pDBInfo)
{
	int result;
	if (pDBInfo->db != NULL)
	{
		if ((result=pDBInfo->db->close(pDBInfo->db, 0)) != 0)
		{
			logError("file: "__FILE__", line: %d, " \
				"db_close fail, " \
				"errno: %d, error info: %s", \
				__LINE__, result, db_strerror(result));
		}
		pDBInfo->db = NULL;
	}

	if (pDBInfo->env != NULL)
	{
		if ((result=pDBInfo->env->close(pDBInfo->env, 0)) != 0)
		{
			logError("file: "__FILE__", line: %d, " \
				"db_env_close fail, " \
				"errno: %d, error info: %s", \
				__LINE__, result, db_strerror(result));
		}
		pDBInfo->env = NULL;
	}

	return 0;
}

int db_set(DBInfo *pDBInfo, const char *pKey, const int key_len, \
	const char *pValue, const int value_len)
{
	int result;
	DBT key;
	DBT value;

	memset(&key, 0, sizeof(key));
	memset(&value, 0, sizeof(value));

	//key.flags = DB_DBT_USERMEM;
	key.data = (char *)pKey;
	key.size = key_len;

	//value.flags = DB_DBT_USERMEM;
	value.data = (char *)pValue;
	value.size = value_len;

	if ((result=pDBInfo->db->put(pDBInfo->db, NULL, &key,  &value, 0)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"db_put fail, " \
			"errno: %d, error info: %s", \
			__LINE__, result, db_strerror(result));
		return EFAULT;
	}

	return result;
}

int db_get(DBInfo *pDBInfo, const char *pKey, const int key_len, \
		char **ppValue, int *size)
{
	int result;
	DBT key;
	DBT value;

	memset(&key, 0, sizeof(key));
	memset(&value, 0, sizeof(value));

	key.data = (char *)pKey;
	key.size = key_len;

	if (*ppValue != NULL)
	{
		value.flags = DB_DBT_USERMEM;
		value.data = *ppValue;
		value.ulen = *size;
	}
	else
	{
		value.flags = DB_DBT_MALLOC;
	}

	if ((result=pDBInfo->db->get(pDBInfo->db, NULL, &key,  &value, 0)) != 0)
	{
		if (result == DB_NOTFOUND)
		{
			return ENOENT;
		}
		else
		{
			logError("file: "__FILE__", line: %d, " \
				"db_get fail, " \
				"errno: %d, error info: %s", \
				__LINE__, result, db_strerror(result));
			return EFAULT;
		}
	}
	else
	{
		*ppValue = value.data;
		*size = value.size;
	}

	return result;
}

int db_delete(DBInfo *pDBInfo, const char *pKey, const int key_len)
{
	int result;
	DBT key;

	memset(&key, 0, sizeof(key));
	//key.flags = DB_DBT_USERMEM;
	key.data = (char *)pKey;
	key.size = key_len;

	if ((result=pDBInfo->db->del(pDBInfo->db, NULL, &key, 0)) != 0)
	{
		logError("file: "__FILE__", line: %d, " \
			"db_del fail, " \
			"errno: %d, error info: %s", \
			__LINE__, result, db_strerror(result));
	}

	return result;
}

int db_inc(DBInfo *pDBInfo, const char *pKey, const int key_len, \
	const int inc, char *pValue, int *value_len)
{
	int64_t n;
	int result;

	if ((result=db_get(pDBInfo, pKey, key_len, \
               	&pValue, value_len)) != 0)
	{
		return result;
	}

	pValue[*value_len] = '\0';
	n = strtoll(pValue, NULL, 10);
	n += inc;

	*value_len = sprintf(pValue, INT64_PRINTF_FORMAT, n);
	return db_set(pDBInfo, pKey, key_len, pValue, *value_len);
}

