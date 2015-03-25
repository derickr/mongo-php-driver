/*
  +---------------------------------------------------------------------------+
  | PHP Driver for MongoDB                                                    |
  +---------------------------------------------------------------------------+
  | Copyright 2013-2014 MongoDB, Inc.                                         |
  |                                                                           |
  | Licensed under the Apache License, Version 2.0 (the "License");           |
  | you may not use this file except in compliance with the License.          |
  | You may obtain a copy of the License at                                   |
  |                                                                           |
  | http://www.apache.org/licenses/LICENSE-2.0                                |
  |                                                                           |
  | Unless required by applicable law or agreed to in writing, software       |
  | distributed under the License is distributed on an "AS IS" BASIS,         |
  | WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  |
  | See the License for the specific language governing permissions and       |
  | limitations under the License.                                            |
  +---------------------------------------------------------------------------+
  | Copyright (c) 2014, MongoDB, Inc.                                         |
  +---------------------------------------------------------------------------+
*/

#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif

/* External libs */
#include "bson.h"
#include "mongoc.h"
#include "mongoc-cursor-cursorid-private.h"
#include "mongoc-read-prefs-private.h"
#include "mongoc-bulk-operation-private.h"
#include "mongoc-trace.h"


/* PHP Core stuff */
#include <php.h>
#include <php_ini.h>
#include <ext/standard/info.h>
#include <ext/standard/file.h>
#include <Zend/zend_hash.h>
#include <Zend/zend_interfaces.h>
#include <Zend/zend_exceptions.h>
#include <ext/spl/spl_iterators.h>
#include <ext/spl/spl_exceptions.h>
/* For formating timestamp in the log */
#include <ext/date/php_date.h>
/* Stream wrapper */
#include <main/php_streams.h>
#include <main/php_network.h>
/* Our Compatability header */
#include "php_compat_53.h"

/* Our stuffz */
#include "php_phongo.h"
#include "php_bson.h"
#include "php_array.h"

#undef MONGOC_LOG_DOMAIN
#define MONGOC_LOG_DOMAIN "PHONGO"

ZEND_DECLARE_MODULE_GLOBALS(mongodb)

/* {{{ phongo_std_object_handlers */
zend_object_handlers phongo_std_object_handlers;

PHONGO_API zend_object_handlers *phongo_get_std_object_handlers(void)
{
	return &phongo_std_object_handlers;
}
/* }}} */

/* {{{ Error reporting and logging */
zend_class_entry* phongo_exception_from_phongo_domain(php_phongo_error_domain_t domain)
{
	switch (domain) {
		case PHONGO_ERROR_INVALID_ARGUMENT:
			return php_phongo_invalidargumentexception_ce;
		case PHONGO_ERROR_RUNTIME:
			return php_phongo_runtimeexception_ce;
		case PHONGO_ERROR_MONGOC_FAILED:
			return php_phongo_runtimeexception_ce;
		case PHONGO_ERROR_WRITE_FAILED:
			return php_phongo_bulkwriteexception_ce;
		case PHONGO_ERROR_WRITE_SINGLE_FAILED:
			return php_phongo_writeexception_ce;
		case PHONGO_ERROR_WRITECONCERN_FAILED:
			return php_phongo_writeconcernexception_ce;
		case PHONGO_ERROR_CONNECTION_FAILED:
			return php_phongo_connectionexception_ce;
	}

	mongoc_log(MONGOC_LOG_LEVEL_ERROR, MONGOC_LOG_DOMAIN, "Resolving unknown exception domain!!!");
	return spl_ce_RuntimeException;
}
zend_class_entry* phongo_exception_from_mongoc_domain(uint32_t /* mongoc_error_domain_t */ domain, uint32_t /* mongoc_error_code_t */ code)
{
	switch(code) {
		case 50: /* ExceededTimeLimit */
			return php_phongo_executiontimeoutexception_ce;
		case MONGOC_ERROR_STREAM_SOCKET:
			return php_phongo_connectiontimeoutexception_ce;
		case 11000: /* DuplicateKey */
			return php_phongo_duplicatekeyexception_ce;
		case MONGOC_ERROR_CLIENT_AUTHENTICATE:
			return php_phongo_authenticationexception_ce;

		case MONGOC_ERROR_STREAM_INVALID_TYPE:
		case MONGOC_ERROR_STREAM_INVALID_STATE:
		case MONGOC_ERROR_STREAM_NAME_RESOLUTION:
		case MONGOC_ERROR_STREAM_CONNECT:
		case MONGOC_ERROR_STREAM_NOT_ESTABLISHED:
			return php_phongo_connectionexception_ce;
		case MONGOC_ERROR_CLIENT_NOT_READY:
		case MONGOC_ERROR_CLIENT_TOO_BIG:
		case MONGOC_ERROR_CLIENT_TOO_SMALL:
		case MONGOC_ERROR_CLIENT_GETNONCE:
		case MONGOC_ERROR_CLIENT_NO_ACCEPTABLE_PEER:
		case MONGOC_ERROR_CLIENT_IN_EXHAUST:
		case MONGOC_ERROR_PROTOCOL_INVALID_REPLY:
		case MONGOC_ERROR_PROTOCOL_BAD_WIRE_VERSION:
		case MONGOC_ERROR_CURSOR_INVALID_CURSOR:
		case MONGOC_ERROR_QUERY_FAILURE:
		/*case MONGOC_ERROR_PROTOCOL_ERROR:*/
		case MONGOC_ERROR_BSON_INVALID:
		case MONGOC_ERROR_MATCHER_INVALID:
		case MONGOC_ERROR_NAMESPACE_INVALID:
		case MONGOC_ERROR_COMMAND_INVALID_ARG:
		case MONGOC_ERROR_COLLECTION_INSERT_FAILED:
		case MONGOC_ERROR_GRIDFS_INVALID_FILENAME:
		case MONGOC_ERROR_QUERY_COMMAND_NOT_FOUND:
		case MONGOC_ERROR_QUERY_NOT_TAILABLE:
			return php_phongo_runtimeexception_ce;
	}
	switch (domain) {
		case MONGOC_ERROR_CLIENT:
		case MONGOC_ERROR_STREAM:
		case MONGOC_ERROR_PROTOCOL:
		case MONGOC_ERROR_CURSOR:
		case MONGOC_ERROR_QUERY:
		case MONGOC_ERROR_INSERT:
		case MONGOC_ERROR_SASL:
		case MONGOC_ERROR_BSON:
		case MONGOC_ERROR_MATCHER:
		case MONGOC_ERROR_NAMESPACE:
		case MONGOC_ERROR_COMMAND:
		case MONGOC_ERROR_COLLECTION:
		case MONGOC_ERROR_GRIDFS:
			/* FIXME: We don't have the Exceptions mocked yet.. */
#if 0
			return phongo_ce_mongo_connection_exception;
#endif
		default:
			return spl_ce_RuntimeException;
	}
}
PHONGO_API zval* phongo_throw_exception(php_phongo_error_domain_t domain TSRMLS_DC, const char *format, ...)
{
	zval *return_value;
	va_list args;
	char *message;
	int message_len;

	va_start(args, format);
	message_len = vspprintf(&message, 0, format, args);
	return_value = zend_throw_exception(phongo_exception_from_phongo_domain(domain), message, 0 TSRMLS_CC);
	efree(message);
	va_end(args);


	return return_value;
}
PHONGO_API zval* phongo_throw_exception_from_bson_error_t(bson_error_t *error TSRMLS_DC)
{
	return zend_throw_exception(phongo_exception_from_mongoc_domain(error->domain, error->code), error->message, error->code TSRMLS_CC);
}
static void php_phongo_log(mongoc_log_level_t log_level, const char *log_domain, const char *message, void *user_data)
{
	TSRMLS_FETCH_FROM_CTX(user_data);
	(void)user_data;

	switch(log_level) {
	case MONGOC_LOG_LEVEL_ERROR:
	case MONGOC_LOG_LEVEL_CRITICAL:
		phongo_throw_exception(PHONGO_ERROR_MONGOC_FAILED TSRMLS_CC, "%s", message);
		return;

	case MONGOC_LOG_LEVEL_WARNING:
	case MONGOC_LOG_LEVEL_MESSAGE:
	case MONGOC_LOG_LEVEL_INFO:
	case MONGOC_LOG_LEVEL_DEBUG:
	case MONGOC_LOG_LEVEL_TRACE:
		{
			int fd = 0;
			char *dt = NULL;

			if (!MONGODB_G(debug_log)) {
				return;
			}
			if (strcasecmp(MONGODB_G(debug_log), "off") == 0) {
				return;
			}
			if (strcasecmp(MONGODB_G(debug_log), "0") == 0) {
				return;
			}

#define PHONGO_DEBUG_LOG_FORMAT "[%s] %10s: %-8s> %s\n"

			dt = php_format_date((char *)"Y-m-d\\TH:i:sP", strlen("Y-m-d\\TH:i:sP"), time(NULL), 0 TSRMLS_CC);
			if (strcasecmp(MONGODB_G(debug_log), "stderr") == 0) {
				fprintf(stderr, PHONGO_DEBUG_LOG_FORMAT, dt, log_domain, mongoc_log_level_str(log_level), message);
			} else if (strcasecmp(MONGODB_G(debug_log), "stdout") == 0) {
				php_printf(PHONGO_DEBUG_LOG_FORMAT, dt, log_domain, mongoc_log_level_str(log_level), message);
			} else if ((fd = VCWD_OPEN_MODE(MONGODB_G(debug_log), O_CREAT | O_APPEND | O_WRONLY, 0644)) != -1) {
				char *tmp;
				int len;

				len = spprintf(&tmp, 0, PHONGO_DEBUG_LOG_FORMAT, dt, log_domain, mongoc_log_level_str(log_level), message);
#ifdef PHP_WIN32
				php_flock(fd, 2);
#endif
				write(fd, tmp, len);
				efree(tmp);
				close(fd);
			}
			efree(dt);
		} break;
	}
}

void phongo_log_writer(mongoc_stream_t *stream, int32_t timeout_msec, ssize_t sent, size_t iovcnt)
{
	php_phongo_stream_socket *base_stream = (php_phongo_stream_socket *)stream;

	/* FIXME: In a ReplicaSet, node->stream is not guranteed to be the wrapped stream, only the raw mongoc_stream_t */
	/*mongoc_log(MONGOC_LOG_LEVEL_MESSAGE, MONGOC_LOG_DOMAIN, "Wrote %zd bytes to '%s:%d' in %zd iterations", sent, base_stream->host->host, base_stream->host->port, iovcnt);*/
}

php_phongo_stream_logger phongo_stream_logger = {
	phongo_log_writer,
};

/* }}} */

/* {{{ Init objects */
void phongo_result_init(zval *return_value, mongoc_cursor_t *cursor, const bson_t *bson, mongoc_client_t *client, zend_bool is_command_cursor TSRMLS_DC) /* {{{ */
{
	php_phongo_result_t *result;

	object_init_ex(return_value, php_phongo_result_ce);

	result = (php_phongo_result_t *)zend_object_store_get_object(return_value TSRMLS_CC);
	result->cursor = cursor;
	result->server_id = mongoc_cursor_get_hint(cursor);
	result->client = client;
	result->is_command_cursor = is_command_cursor;
	result->firstBatch = bson ? bson_copy(bson) : NULL;
} /* }}} */

void phongo_server_init(zval *return_value, mongoc_client_t *client, int server_id TSRMLS_DC) /* {{{ */
{
	php_phongo_server_t *server;

	object_init_ex(return_value, php_phongo_server_ce);

	server = (php_phongo_server_t *)zend_object_store_get_object(return_value TSRMLS_CC);
	server->client = client;
	server->server_id = server_id;
}
/* }}} */

zend_bool phongo_writeconcernerror_init(zval *return_value, bson_t *bson TSRMLS_DC) /* {{{ */
{
	bson_iter_t iter;
	php_phongo_writeconcernerror_t *writeconcernerror;

	writeconcernerror = (php_phongo_writeconcernerror_t *)zend_object_store_get_object(return_value TSRMLS_CC);

	if (bson_iter_init_find(&iter, bson, "code") && BSON_ITER_HOLDS_INT32(&iter)) {
		writeconcernerror->code = bson_iter_int32(&iter);
	}
	if (bson_iter_init_find(&iter, bson, "errmsg") && BSON_ITER_HOLDS_UTF8(&iter)) {
		writeconcernerror->message = bson_iter_dup_utf8(&iter, NULL);
	}
	if (bson_iter_init_find(&iter, bson, "errInfo") && BSON_ITER_HOLDS_DOCUMENT(&iter)) {
		uint32_t               len;
		const uint8_t         *data;
		php_phongo_bson_state  state = PHONGO_BSON_STATE_INITIALIZER;

		bson_iter_document(&iter, &len, &data);

		if (!data) {
			return false;
		}

		MAKE_STD_ZVAL(writeconcernerror->info);
		state.zchild = writeconcernerror->info;

		if (!bson_to_zval(data, len, &state)) {
			zval_ptr_dtor(&writeconcernerror->info);
			writeconcernerror->info = NULL;

			return false;
		}
	}

	return true;
} /* }}} */

zend_bool phongo_writeerror_init(zval *return_value, bson_t *bson TSRMLS_DC) /* {{{ */
{
	bson_iter_t iter;
	php_phongo_writeerror_t *writeerror;

	writeerror = (php_phongo_writeerror_t *)zend_object_store_get_object(return_value TSRMLS_CC);

	if (bson_iter_init_find(&iter, bson, "code") && BSON_ITER_HOLDS_INT32(&iter)) {
		writeerror->code = bson_iter_int32(&iter);
	}
	if (bson_iter_init_find(&iter, bson, "errmsg") && BSON_ITER_HOLDS_UTF8(&iter)) {
		writeerror->message = bson_iter_dup_utf8(&iter, NULL);
	}
	if (bson_iter_init_find(&iter, bson, "errInfo")) {
		bson_t                 info;
		php_phongo_bson_state  state = PHONGO_BSON_STATE_INITIALIZER;

		MAKE_STD_ZVAL(writeerror->info);
		state.zchild = writeerror->info;

		bson_init(&info);
		bson_append_iter(&info, NULL, 0, &iter);

		if (!bson_to_zval(bson_get_data(&info), info.len, &state)) {
			zval_ptr_dtor(&writeerror->info);
			writeerror->info = NULL;
			return false;
		}
	}
	if (bson_iter_init_find(&iter, bson, "index") && BSON_ITER_HOLDS_INT32(&iter)) {
		writeerror->index = bson_iter_int32(&iter);
	}

	return true;
} /* }}} */

php_phongo_writeresult_t *phongo_writeresult_init(zval *return_value, mongoc_write_result_t *write_result, mongoc_client_t *client, int server_id TSRMLS_DC) /* {{{ */
{
	php_phongo_writeresult_t *writeresult;

	object_init_ex(return_value, php_phongo_writeresult_ce);

	writeresult = (php_phongo_writeresult_t *)zend_object_store_get_object(return_value TSRMLS_CC);
	writeresult->client    = client;
	writeresult->server_id = server_id;

	/* Copy write_results or else it'll get destroyed with the bulk destruction */
#define SCP(field) writeresult->write_result.field = write_result->field
	SCP(omit_nModified);
	SCP(nInserted);
	SCP(nMatched);
	SCP(nModified);
	SCP(nRemoved);
	SCP(nUpserted);
	SCP(offset);
	SCP(n_commands);

	bson_copy_to(&write_result->upserted,          &writeresult->write_result.upserted);
	bson_copy_to(&write_result->writeConcernError, &writeresult->write_result.writeConcernError);
	bson_copy_to(&write_result->writeErrors,       &writeresult->write_result.writeErrors);
	SCP(upsert_append_count);
#undef SCP

	return writeresult;

} /* }}} */

/* }}} */

/* {{{ CRUD */
/* Splits a namespace name into the database and collection names, allocated with estrdup. */
bool phongo_split_namespace(const char *namespace, char **dbname, char **cname) /* {{{ */
{
	char *dot = strchr(namespace, '.');

	if (!dot) {
		return false;
	}

	if (cname) {
		*cname = estrdup(namespace + (dot - namespace) + 1);
	}
	if (dbname) {
		*dbname = estrndup(namespace, dot - namespace);
	}

	return true;
} /* }}} */

mongoc_bulk_operation_t *phongo_bulkwrite_init(zend_bool ordered) { /* {{{ */
	return mongoc_bulk_operation_new(ordered);
} /* }}} */

void phongo_unwrap_exception(bool retval, zval *return_value TSRMLS_DC)
{
	if (!retval) {
		if (instanceof_function(Z_OBJCE_P(EG(exception)), php_phongo_bulkwriteexception_ce TSRMLS_CC)) {
			php_phongo_writeresult_t *wr = php_phongo_writeresult_get_from_bulkwriteexception(EG(exception) TSRMLS_CC);

			/* Clear the BulkWriteException */
			zend_clear_exception(TSRMLS_C);

			/* Throw WriteError and/or WriteConcernErrors */
			php_phongo_throw_write_errors(wr TSRMLS_CC);
			php_phongo_throw_write_concern_error(wr TSRMLS_CC);

			if (instanceof_function(Z_OBJCE_P(EG(exception)), php_phongo_writeexception_ce TSRMLS_CC)) {
				zend_update_property(Z_OBJCE_P(EG(exception)), EG(exception), ZEND_STRL("writeResult"), return_value TSRMLS_CC);
			}
		}
	}
}

int phongo_execute_single_insert(mongoc_client_t *client, const char *namespace, const bson_t *doc, const mongoc_write_concern_t *write_concern, int server_id, zval *return_value, int return_value_used TSRMLS_DC) /* {{{ */
{
	bool retval = false;
	mongoc_bulk_operation_t *bulk;

	bulk = phongo_bulkwrite_init(true);
	mongoc_bulk_operation_insert(bulk, doc);

	retval = phongo_execute_write(client, namespace, bulk, write_concern, server_id, return_value, return_value_used TSRMLS_CC);
	mongoc_bulk_operation_destroy(bulk);

	phongo_unwrap_exception(retval, return_value TSRMLS_CC);
	return retval;
} /* }}} */

int phongo_execute_single_update(mongoc_client_t *client, const char *namespace, const bson_t *query, const bson_t *update, const mongoc_write_concern_t *write_concern, int server_id, mongoc_update_flags_t flags, zval *return_value, int return_value_used TSRMLS_DC) /* {{{ */
{
	bool retval = false;
	mongoc_bulk_operation_t *bulk;

	bulk = phongo_bulkwrite_init(true);
	if (flags & MONGOC_UPDATE_MULTI_UPDATE) {
		mongoc_bulk_operation_update_one(bulk, query, update, !!(flags & MONGOC_UPDATE_UPSERT));
	} else {
		mongoc_bulk_operation_update(bulk, query, update, !!(flags & MONGOC_UPDATE_UPSERT));
	}
	retval = phongo_execute_write(client, namespace, bulk, write_concern, server_id, return_value, return_value_used TSRMLS_CC);
	mongoc_bulk_operation_destroy(bulk);

	phongo_unwrap_exception(retval, return_value TSRMLS_CC);
	return retval;
} /* }}} */

int phongo_execute_single_delete(mongoc_client_t *client, const char *namespace, const bson_t *query, const mongoc_write_concern_t *write_concern, int server_id, mongoc_delete_flags_t flags, zval *return_value, int return_value_used TSRMLS_DC) /* {{{ */
{
	bool retval = false;
	mongoc_bulk_operation_t *bulk;

	bulk = phongo_bulkwrite_init(true);
	if (flags & MONGOC_DELETE_SINGLE_REMOVE) {
		mongoc_bulk_operation_remove_one(bulk, query);
	} else {
		mongoc_bulk_operation_remove(bulk, query);
	}

	retval = phongo_execute_write(client, namespace, bulk, write_concern, server_id, return_value, return_value_used TSRMLS_CC);
	mongoc_bulk_operation_destroy(bulk);

	phongo_unwrap_exception(retval, return_value TSRMLS_CC);
	return retval;
} /* }}} */

bool phongo_execute_write(mongoc_client_t *client, const char *namespace, mongoc_bulk_operation_t *bulk, const mongoc_write_concern_t *write_concern, int server_id, zval *return_value, int return_value_used TSRMLS_DC) /* {{{ */
{
	bson_error_t error;
	char *dbname;
	char *collname;
	int success;
	php_phongo_writeresult_t *writeresult;

	if (!phongo_split_namespace(namespace, &dbname, &collname)) {
		phongo_throw_exception(PHONGO_ERROR_INVALID_ARGUMENT TSRMLS_CC, "%s", "Invalid namespace provided");
		return false;
	}

	mongoc_bulk_operation_set_database(bulk, dbname);
	mongoc_bulk_operation_set_collection(bulk, collname);
	mongoc_bulk_operation_set_client(bulk, client);

	/* If a write concern was not specified, libmongoc will use the client's
	 * write concern; however, we should still fetch it for the write result. */
	if (write_concern) {
		mongoc_bulk_operation_set_write_concern(bulk, write_concern);
	} else {
		write_concern = mongoc_client_get_write_concern(client);
	}

	efree(dbname);
	efree(collname);

	if (server_id > 0) {
		mongoc_bulk_operation_set_hint(bulk, server_id);
	}

	success = mongoc_bulk_operation_execute(bulk, NULL, &error);

	/* Write succeeded and the user doesn't care for the results */
	if (success && !return_value_used) {
		return true;
	}

	/* Check for connection related exceptions */
	if (EG(exception)) {
		return false;
	}

	writeresult = phongo_writeresult_init(return_value, &bulk->result, client, bulk->hint TSRMLS_CC);
	writeresult->write_concern = mongoc_write_concern_copy(write_concern);

	/* The Write failed */
	if (!success) {
		/* The Command itself failed */
		if (
				bson_empty0(&writeresult->write_result.writeErrors)
				&& bson_empty0(&writeresult->write_result.writeConcernError)
			) {
			/* FIXME: Maybe we can look at write_result.error and not pass error at all? */
			phongo_throw_exception_from_bson_error_t(&error TSRMLS_CC);
		} else {
			zval *ex;

			ex = phongo_throw_exception(PHONGO_ERROR_WRITE_FAILED TSRMLS_CC, "BulkWrite error");
			zend_update_property(Z_OBJCE_P(ex), ex, ZEND_STRL("writeResult"), return_value TSRMLS_CC);
		}
		return false;
	}
	return true;
} /* }}} */

int phongo_execute_query(mongoc_client_t *client, const char *namespace, const php_phongo_query_t *query, const mongoc_read_prefs_t *read_preference, int server_id, zval *return_value, int return_value_used TSRMLS_DC) /* {{{ */
{
	const bson_t *doc = NULL;
	mongoc_cursor_t *cursor;
	char *dbname;
	char *collname;
	mongoc_collection_t *collection;

	if (!phongo_split_namespace(namespace, &dbname, &collname)) {
		phongo_throw_exception(PHONGO_ERROR_INVALID_ARGUMENT TSRMLS_CC, "%s", "Invalid namespace provided");
		return false;
	}
	collection = mongoc_client_get_collection(client, dbname, collname);
	efree(dbname);
	efree(collname);

	cursor = mongoc_collection_find(collection, query->flags, query->skip, query->limit, query->batch_size, query->query, query->selector, read_preference);
	mongoc_collection_destroy(collection);

	/* mongoc issues a warning we need to catch somehow */
	if (!cursor) {
		phongo_throw_exception(PHONGO_ERROR_MONGOC_FAILED TSRMLS_CC, "%s", "FIXME: Couldn't create cursor...");
		return false;
	}

	cursor->hint = server_id;
	if (!mongoc_cursor_next(cursor, &doc)) {
		bson_error_t error;

		/* Could simply be no docs, which is not an error */
		if (mongoc_cursor_error(cursor, &error)) {
			phongo_throw_exception_from_bson_error_t(&error TSRMLS_CC);
			mongoc_cursor_destroy(cursor);
			return false;
		}

	}

	if (!return_value_used) {
		mongoc_cursor_destroy(cursor);
		return true;
	}

	phongo_result_init(return_value, cursor, doc, client, 0 TSRMLS_CC);
	return true;
} /* }}} */

int phongo_execute_command(mongoc_client_t *client, const char *db, const bson_t *command, const mongoc_read_prefs_t *read_preference, int server_id, zval *return_value, int return_value_used TSRMLS_DC) /* {{{ */
{
	mongoc_cursor_t *cursor;
	const bson_t *doc;
	bson_iter_t iter;
	bson_iter_t child;


	cursor = mongoc_client_command(client, db, MONGOC_QUERY_NONE, 0, 1, 0, command, NULL, read_preference);
	cursor->hint = server_id;

	if (!mongoc_cursor_next(cursor, &doc)) {
		bson_error_t error;

		if (mongoc_cursor_error(cursor, &error)) {
			mongoc_cursor_destroy(cursor);
			phongo_throw_exception_from_bson_error_t(&error TSRMLS_CC);
			return false;
		}
	}

	if (!return_value_used) {
		mongoc_cursor_destroy(cursor);
		return true;
	}

	/* Detect if its an command cursor */
	if (bson_iter_init_find (&iter, doc, "cursor") && BSON_ITER_HOLDS_DOCUMENT (&iter) && bson_iter_recurse (&iter, &child)) {
		while (bson_iter_next (&child)) {
			if (BSON_ITER_IS_KEY (&child, "id")) {
				cursor->rpc.reply.cursor_id = bson_iter_as_int64 (&child);
			} else if (BSON_ITER_IS_KEY (&child, "ns")) {
				const char *ns;

				ns = bson_iter_utf8 (&child, &cursor->nslen);
				bson_strncpy (cursor->ns, ns, sizeof cursor->ns);
			} else if (BSON_ITER_IS_KEY (&child, "firstBatch")) {
				if (BSON_ITER_HOLDS_ARRAY (&child)) {
					const uint8_t *data = NULL;
					uint32_t data_len = 0;
					bson_t first_batch;

					bson_iter_array (&child, &data_len, &data);
					if (bson_init_static (&first_batch, data, data_len)) {
						_mongoc_cursor_cursorid_init(cursor);
						cursor->limit = 0;
						cursor->is_command = false;
						phongo_result_init(return_value, cursor, &first_batch, client, 1 TSRMLS_CC);
						return true;
					}
				}
			}
		}
	}

	phongo_result_init(return_value, cursor, doc, client, 0 TSRMLS_CC);
	return true;
} /* }}} */

/* }}} */

/* {{{ Stream vtable */
void phongo_stream_destroy(mongoc_stream_t *stream_wrap) /* {{{ */
{
	php_phongo_stream_socket *base_stream = (php_phongo_stream_socket *)stream_wrap;

	if (base_stream->stream) {
		TSRMLS_FETCH_FROM_CTX(base_stream->tsrm_ls);

		php_stream_free(base_stream->stream, PHP_STREAM_FREE_CLOSE_PERSISTENT | PHP_STREAM_FREE_RSRC_DTOR);
		base_stream->stream = NULL;
	}

	efree(base_stream);
} /* }}} */

int phongo_stream_close(mongoc_stream_t *stream) /* {{{ */
{
	return 0;
} /* }}} */

void php_phongo_set_timeout(php_phongo_stream_socket *base_stream, int32_t timeout_msec) /* {{{ */
{
	struct timeval rtimeout = {0, 0};
	TSRMLS_FETCH_FROM_CTX(base_stream->tsrm_ls);

	if (timeout_msec > 0) {
		rtimeout.tv_sec = timeout_msec / 1000;
		rtimeout.tv_usec = (timeout_msec % 1000) * 1000;
	}

	php_stream_set_option(base_stream->stream, PHP_STREAM_OPTION_READ_TIMEOUT, 0, &rtimeout);
	mongoc_log(MONGOC_LOG_LEVEL_DEBUG, MONGOC_LOG_DOMAIN, "Setting timeout to: %d", timeout_msec);
} /* }}} */

ssize_t phongo_stream_writev(mongoc_stream_t *stream, mongoc_iovec_t *iov, size_t iovcnt, int32_t timeout_msec) /* {{{ */
{
	size_t     i = 0;
	ssize_t sent = 0;
	php_phongo_stream_socket *base_stream = (php_phongo_stream_socket *)stream;
	TSRMLS_FETCH_FROM_CTX(base_stream->tsrm_ls);

	php_phongo_set_timeout(base_stream, timeout_msec);
	for (i = 0; i < iovcnt; i++) {
		sent += php_stream_write(base_stream->stream, iov[i].iov_base, iov[i].iov_len);
	}
	if (base_stream->log.writer) {
		base_stream->log.writer(stream, timeout_msec, sent, iovcnt);
	}

	return sent;
} /* }}} */

ssize_t phongo_stream_readv(mongoc_stream_t *stream, mongoc_iovec_t *iov, size_t iovcnt, size_t min_bytes, int32_t timeout_msec) /* {{{ */
{
	php_phongo_stream_socket *base_stream = (php_phongo_stream_socket *)stream;
	ssize_t ret = 0;
	ssize_t read;
	size_t cur = 0;
	TSRMLS_FETCH_FROM_CTX(base_stream->tsrm_ls);

	php_phongo_set_timeout(base_stream, timeout_msec);

	do {
		read = php_stream_read(base_stream->stream, iov[cur].iov_base, iov[cur].iov_len);
		mongoc_log(MONGOC_LOG_LEVEL_DEBUG, MONGOC_LOG_DOMAIN, "Reading got: %ld wanted: %ld", read, min_bytes);

		if (read <= 0) {
			if (ret >= (ssize_t)min_bytes) {
				break;
			}
			return -1;
		}

		ret += read;

		while ((cur < iovcnt) && (read >= (ssize_t)iov[cur].iov_len)) {
			read -= iov[cur++].iov_len;
		}

		if (cur == iovcnt) {
			break;
		}

		if (ret >= (ssize_t)min_bytes) {
			break;
		}

		iov[cur].iov_base = ((char *)iov[cur].iov_base) + read;
		iov[cur].iov_len -= read;
	} while(1);

	return ret;
} /* }}} */

int phongo_stream_setsockopt(mongoc_stream_t *stream, int level, int optname, void *optval, socklen_t optlen) /* {{{ */
{
	php_phongo_stream_socket *base_stream = (php_phongo_stream_socket *)stream;
	int socket = ((php_netstream_data_t *)base_stream->stream->abstract)->socket;

	return setsockopt (socket, level, optname, optval, optlen);
} /* }}} */

bool phongo_stream_socket_check_closed(mongoc_stream_t *stream) /* {{{ */
{
	php_phongo_stream_socket *base_stream = (php_phongo_stream_socket *)stream;
	TSRMLS_FETCH_FROM_CTX(base_stream->tsrm_ls);

	return PHP_STREAM_OPTION_RETURN_OK == php_stream_set_option(base_stream->stream, PHP_STREAM_OPTION_CHECK_LIVENESS, 0, NULL);
} /* }}} */

mongoc_stream_t* phongo_stream_get_base_stream(mongoc_stream_t *stream) /* {{{ */
{
	return (mongoc_stream_t *) stream;
} /* }}} */

ssize_t phongo_stream_poll (mongoc_stream_poll_t *streams, size_t nstreams, int32_t timeout) /* {{{ */
{
	php_pollfd *fds = NULL;
	size_t i;
	ssize_t rval = -1;
	TSRMLS_FETCH();

	fds = emalloc(sizeof(*fds) * nstreams);
	for (i = 0; i < nstreams; i++) {
		php_socket_t this_fd;

		if (php_stream_cast(((php_phongo_stream_socket *)streams[i].stream)->stream, PHP_STREAM_AS_FD_FOR_SELECT | PHP_STREAM_CAST_INTERNAL, (void*)&this_fd, 0) == SUCCESS && this_fd >= 0) {
			fds[i].fd = this_fd;
			fds[i].events = streams[i].events;
			fds[i].revents = 0;
		}
	}

	rval = php_poll2(fds, nstreams, timeout);

	if (rval > 0) {
		for (i = 0; i < nstreams; i++) {
			streams[i].revents = fds[i].revents;
		}
	}

	efree(fds);

	return rval;
} /* }}} */

mongoc_stream_t* phongo_stream_initiator(const mongoc_uri_t *uri, const mongoc_host_list_t *host, void *user_data, bson_error_t *error) /* {{{ */
{
	php_phongo_stream_socket *base_stream = NULL;
	php_stream *stream = NULL;
	const bson_t *options;
	bson_iter_t iter;
	struct timeval timeout = {0, 0};
	struct timeval *timeoutp = NULL;
	char *uniqid;
	char *errmsg = NULL;
	int errcode;
	char *dsn;
	int dsn_len;
	TSRMLS_FETCH();

	ENTRY;

	switch (host->family) {
#if defined(AF_INET6)
		case AF_INET6:
#endif
		case AF_INET:
			dsn_len = spprintf(&dsn, 0, "tcp://%s:%d", host->host, host->port);
			break;

		case AF_UNIX:
			dsn_len = spprintf(&dsn, 0, "unix://%s", host->host);
			break;

		default:
			bson_set_error (error, MONGOC_ERROR_STREAM, MONGOC_ERROR_STREAM_INVALID_TYPE, "Invalid address family: 0x%02x", host->family);
			RETURN(NULL);
	}

	options = mongoc_uri_get_options(uri);

	if (bson_iter_init_find (&iter, options, "connecttimeoutms") && BSON_ITER_HOLDS_INT32 (&iter)) {
		int32_t connecttimeoutms = MONGOC_DEFAULT_CONNECTTIMEOUTMS;

		if (!(connecttimeoutms = bson_iter_int32(&iter))) {
			connecttimeoutms = MONGOC_DEFAULT_CONNECTTIMEOUTMS;
		}

		timeout.tv_sec = connecttimeoutms / 1000;
		timeout.tv_usec = (connecttimeoutms % 1000) * 1000;

		timeoutp = &timeout;
	}

	spprintf(&uniqid, 0, "mongodb://%s:%s@%s:%d/%s?ssl=%d&authMechanism=%s&authSource=%s",
		mongoc_uri_get_username(uri),
		mongoc_uri_get_password(uri),
		host->host,
		host->port,
		mongoc_uri_get_database(uri),
		mongoc_uri_get_ssl(uri) ? 1 : 0,
		mongoc_uri_get_auth_mechanism(uri),
		mongoc_uri_get_auth_source(uri)
	);

	mongoc_log(MONGOC_LOG_LEVEL_DEBUG, MONGOC_LOG_DOMAIN, "Connecting to '%s'", uniqid);
	stream = php_stream_xport_create(dsn, dsn_len, 0, STREAM_XPORT_CLIENT | STREAM_XPORT_CONNECT | STREAM_XPORT_CONNECT_ASYNC, uniqid, timeoutp, (php_stream_context *)user_data, &errmsg, &errcode);

	efree(uniqid);
	if (!stream) {
		bson_set_error (error, MONGOC_ERROR_STREAM, MONGOC_ERROR_STREAM_CONNECT, "Failed connecting to '%s:%d': %s", host->host, host->port, errmsg);
		efree(dsn);
		if (errmsg) {
			efree(errmsg);
		}
		RETURN(NULL);
	}

	/* Avoid invalid leak warning in debug mode when freeing the stream */
#if ZEND_DEBUG
	stream->__exposed = 1;
#endif

	if (mongoc_uri_get_ssl(uri)) {
		zend_error_handling       error_handling;

		zend_replace_error_handling(EH_THROW, php_phongo_sslconnectionexception_ce, &error_handling TSRMLS_CC);

		mongoc_log(MONGOC_LOG_LEVEL_DEBUG, MONGOC_LOG_DOMAIN, "Enabling SSL");
		if (php_stream_xport_crypto_setup(stream, PHONGO_CRYPTO_METHOD, NULL TSRMLS_CC) < 0) {
			zend_restore_error_handling(&error_handling TSRMLS_CC);
			php_stream_free(stream, PHP_STREAM_FREE_CLOSE_PERSISTENT | PHP_STREAM_FREE_RSRC_DTOR);
			bson_set_error (error, MONGOC_ERROR_STREAM, MONGOC_ERROR_STREAM_INVALID_TYPE, "Failed to setup crypto, is the OpenSSL extension loaded?");
			efree(dsn);
			return NULL;
		}

		if (php_stream_xport_crypto_enable(stream, 1 TSRMLS_CC) < 0) {
			zend_restore_error_handling(&error_handling TSRMLS_CC);
			php_stream_free(stream, PHP_STREAM_FREE_CLOSE_PERSISTENT | PHP_STREAM_FREE_RSRC_DTOR);
			bson_set_error (error, MONGOC_ERROR_STREAM, MONGOC_ERROR_STREAM_INVALID_TYPE, "Failed to setup crypto, is the server running with SSL?");
			efree(dsn);
			return NULL;
		}

		zend_restore_error_handling(&error_handling TSRMLS_CC);
	}
	efree(dsn);


	base_stream = ecalloc(1, sizeof(php_phongo_stream_socket));
	base_stream->stream = stream;
	base_stream->uri = uri;
	base_stream->host = host;
	base_stream->log = phongo_stream_logger;
	TSRMLS_SET_CTX(base_stream->tsrm_ls);

	/* flush missing, doesn't seem to be used */
	base_stream->vtable.type = 100;
	base_stream->vtable.destroy = phongo_stream_destroy;
	base_stream->vtable.close = phongo_stream_close;
	base_stream->vtable.writev = phongo_stream_writev;
	base_stream->vtable.readv = phongo_stream_readv;
	base_stream->vtable.setsockopt = phongo_stream_setsockopt;
	base_stream->vtable.check_closed = phongo_stream_socket_check_closed;
	base_stream->vtable.get_base_stream = phongo_stream_get_base_stream;
	base_stream->vtable.poll = phongo_stream_poll;

	if (host->family != AF_UNIX) {
		int flag = 1;

		if (phongo_stream_setsockopt((mongoc_stream_t *)base_stream, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(int))) {
			mongoc_log(MONGOC_LOG_LEVEL_WARNING, MONGOC_LOG_DOMAIN, "setsockopt TCP_NODELAY failed");
		}
	}

	RETURN((mongoc_stream_t *)base_stream);
} /* }}} */

/* }}} */

const mongoc_write_concern_t* phongo_write_concern_from_zval(zval *zwrite_concern TSRMLS_DC) /* {{{ */
{
	if (zwrite_concern) {
		php_phongo_writeconcern_t *intern = (php_phongo_writeconcern_t *)zend_object_store_get_object(zwrite_concern TSRMLS_CC);

		if (intern) {
			return intern->write_concern;
		}
	}

	return NULL;
} /* }}} */

const mongoc_read_prefs_t* phongo_read_preference_from_zval(zval *zread_preference TSRMLS_DC) /* {{{ */
{
	if (zread_preference) {
		php_phongo_readpreference_t *intern = (php_phongo_readpreference_t *)zend_object_store_get_object(zread_preference TSRMLS_CC);

		if (intern) {
			return intern->read_preference;
		}
	}

	return NULL;
} /* }}} */

const php_phongo_query_t* phongo_query_from_zval(zval *zquery TSRMLS_DC) /* {{{ */
{
	php_phongo_query_t *intern = (php_phongo_query_t *)zend_object_store_get_object(zquery TSRMLS_CC);

	return intern;
} /* }}} */

bool phongo_query_init(php_phongo_query_t *query, zval *filter, zval *options TSRMLS_DC) /* {{{ */
{
	zval *zquery = NULL;

	if (filter && !(Z_TYPE_P(filter) == IS_ARRAY || Z_TYPE_P(filter) == IS_OBJECT)) {
		phongo_throw_exception(PHONGO_ERROR_INVALID_ARGUMENT TSRMLS_CC, "Expected filter to be array or object, %s given", zend_get_type_by_const(Z_TYPE_P(filter)));
		return false;
	}

	MAKE_STD_ZVAL(zquery);
	array_init(zquery);

	if (options) {
		/* TODO: Ensure batchSize, limit, and skip are 32-bit  */
		query->batch_size = php_array_fetchc_long(options, "batchSize");
		query->limit = php_array_fetchc_long(options, "limit");
		query->skip = php_array_fetchc_long(options, "skip");

		query->flags = 0;
		query->flags |= php_array_fetchl_bool(options, ZEND_STRS("tailable"))        ? MONGOC_QUERY_TAILABLE_CURSOR   : 0;
		query->flags |= php_array_fetchl_bool(options, ZEND_STRS("slaveOk"))         ? MONGOC_QUERY_SLAVE_OK          : 0;
		query->flags |= php_array_fetchl_bool(options, ZEND_STRS("oplogReplay"))     ? MONGOC_QUERY_OPLOG_REPLAY      : 0;
		query->flags |= php_array_fetchl_bool(options, ZEND_STRS("noCursorTimeout")) ? MONGOC_QUERY_NO_CURSOR_TIMEOUT : 0;
		query->flags |= php_array_fetchl_bool(options, ZEND_STRS("awaitData"))       ? MONGOC_QUERY_AWAIT_DATA        : 0;
		query->flags |= php_array_fetchl_bool(options, ZEND_STRS("exhaust"))         ? MONGOC_QUERY_EXHAUST           : 0;
		query->flags |= php_array_fetchl_bool(options, ZEND_STRS("partial"))         ? MONGOC_QUERY_PARTIAL           : 0;


		if (php_array_existsc(options, "modifiers")) {
			zval *modifiers = php_array_fetchc(options, "modifiers");

			if (modifiers && !(Z_TYPE_P(modifiers) == IS_ARRAY || Z_TYPE_P(modifiers) == IS_OBJECT)) {
				phongo_throw_exception(PHONGO_ERROR_INVALID_ARGUMENT TSRMLS_CC, "Expected modifiers to be array or object, %s given", zend_get_type_by_const(Z_TYPE_P(modifiers)));
				zval_ptr_dtor(&zquery);
				return false;
			}

			convert_to_array_ex(&modifiers);
			zend_hash_merge(HASH_OF(zquery), HASH_OF(modifiers), (void (*)(void*))zval_add_ref, NULL, sizeof(zval *), 1);
		}

		if (php_array_existsc(options, "projection")) {
			zval *projection = php_array_fetchc(options, "projection");

			if (projection && !(Z_TYPE_P(projection) == IS_ARRAY || Z_TYPE_P(projection) == IS_OBJECT)) {
				phongo_throw_exception(PHONGO_ERROR_INVALID_ARGUMENT TSRMLS_CC, "Expected projection to be array or object, %s given", zend_get_type_by_const(Z_TYPE_P(projection)));
				zval_ptr_dtor(&zquery);
				return false;
			}

			convert_to_array_ex(&projection);
			query->selector = bson_new();
			zval_to_bson(projection, PHONGO_BSON_NONE, query->selector, NULL TSRMLS_CC);
		}

		if (php_array_existsc(options, "sort")) {
			zval *sort = php_array_fetchc(options, "sort");

			if (sort && !(Z_TYPE_P(sort) == IS_ARRAY || Z_TYPE_P(sort) == IS_OBJECT)) {
				phongo_throw_exception(PHONGO_ERROR_INVALID_ARGUMENT TSRMLS_CC, "Expected sort to be array or object, %s given", zend_get_type_by_const(Z_TYPE_P(sort)));
				zval_ptr_dtor(&zquery);
				return false;
			}

			convert_to_array_ex(&sort);
			Z_ADDREF_P(sort);
			add_assoc_zval_ex(zquery, ZEND_STRS("$orderby"), sort);
		}
	}

	Z_ADDREF_P(filter);
	add_assoc_zval_ex(zquery, ZEND_STRS("$query"), filter);

	query->query = bson_new();
	zval_to_bson(zquery, PHONGO_BSON_NONE, query->query, NULL TSRMLS_CC);
	zval_ptr_dtor(&zquery);

	return true;
} /* }}} */

void php_phongo_cursor_id_new_from_id(zval *object, int64_t cursorid TSRMLS_DC) /* {{{ */
{
	php_phongo_cursorid_t     *intern;

	object_init_ex(object, php_phongo_cursorid_ce);

	intern = (php_phongo_cursorid_t *)zend_object_store_get_object(object TSRMLS_CC);
	intern->id = cursorid;
} /* }}} */

void php_phongo_cursor_new_from_result(zval *object, php_phongo_result_t *result TSRMLS_DC) /* {{{ */
{
	php_phongo_cursor_t     *intern;

	object_init_ex(object, php_phongo_cursor_ce);

	intern = (php_phongo_cursor_t *)zend_object_store_get_object(object TSRMLS_CC);
	intern->result = result;
} /* }}} */

void php_phongo_objectid_new_from_oid(zval *object, const bson_oid_t *oid TSRMLS_DC) /* {{{ */
{
	php_phongo_objectid_t     *intern;

	object_init_ex(object, php_phongo_objectid_ce);

	intern = (php_phongo_objectid_t *)zend_object_store_get_object(object TSRMLS_CC);
	bson_oid_to_string(oid, intern->oid);
} /* }}} */


void php_phongo_read_preference_to_zval(zval *retval, const mongoc_read_prefs_t *read_prefs) /* {{{ */
{

	array_init_size(retval, 2);

	add_assoc_long_ex(retval, ZEND_STRS("mode"), read_prefs->mode);
	if (read_prefs->tags.len) {
		php_phongo_bson_state  state = PHONGO_BSON_STATE_INITIALIZER;

		MAKE_STD_ZVAL(state.zchild);
		bson_to_zval(bson_get_data(&read_prefs->tags), read_prefs->tags.len, &state);
		add_assoc_zval_ex(retval, ZEND_STRS("tags"), state.zchild);
	} else {
		add_assoc_null_ex(retval, ZEND_STRS("tags"));
	}
} /* }}} */

void php_phongo_write_concern_to_zval(zval *retval, const mongoc_write_concern_t *write_concern) /* {{{ */
{
	const char *wtag = mongoc_write_concern_get_wtag(write_concern);
	const int32_t w = mongoc_write_concern_get_w(write_concern);

	array_init_size(retval, 5);

	if (wtag) {
		add_assoc_string_ex(retval, ZEND_STRS("w"), (char *)wtag, 1);
	} else if (mongoc_write_concern_get_wmajority(write_concern)) {
		add_assoc_string_ex(retval, ZEND_STRS("w"), (char *)"majority", 1);
	} else if (w != MONGOC_WRITE_CONCERN_W_DEFAULT) {
		add_assoc_long_ex(retval, ZEND_STRS("w"), w);
	}

	add_assoc_bool_ex(retval, ZEND_STRS("wmajority"), mongoc_write_concern_get_wmajority(write_concern));
	add_assoc_long_ex(retval, ZEND_STRS("wtimeout"), mongoc_write_concern_get_wtimeout(write_concern));
	add_assoc_bool_ex(retval, ZEND_STRS("fsync"), mongoc_write_concern_get_fsync(write_concern));
	add_assoc_bool_ex(retval, ZEND_STRS("journal"), mongoc_write_concern_get_journal(write_concern));
} /* }}} */


void php_phongo_result_to_zval(zval *retval, php_phongo_result_t *result) /* {{{ */
{

	array_init_size(retval, 4);

	if (result->cursor) {
		zval *cursor = NULL;

		MAKE_STD_ZVAL(cursor);
		array_init_size(cursor, 19);

		add_assoc_long_ex(cursor, ZEND_STRS("stamp"), result->cursor->stamp);

#define _ADD_BOOL(z, field) add_assoc_bool_ex(z, ZEND_STRS(#field), result->cursor->field)
		_ADD_BOOL(cursor, is_command);
		_ADD_BOOL(cursor, sent);
		_ADD_BOOL(cursor, done);
		_ADD_BOOL(cursor, failed);
		_ADD_BOOL(cursor, end_of_event);
		_ADD_BOOL(cursor, in_exhaust);
		_ADD_BOOL(cursor, redir_primary);
		_ADD_BOOL(cursor, has_fields);
#undef _ADD_BOOL

		{
			php_phongo_bson_state  state = PHONGO_BSON_STATE_INITIALIZER;

			MAKE_STD_ZVAL(state.zchild);
			bson_to_zval(bson_get_data(&result->cursor->query), result->cursor->query.len, &state);
			add_assoc_zval_ex(cursor, ZEND_STRS("query"), state.zchild);
		}
		{
			php_phongo_bson_state  state = PHONGO_BSON_STATE_INITIALIZER;

			MAKE_STD_ZVAL(state.zchild);
			bson_to_zval(bson_get_data(&result->cursor->fields), result->cursor->fields.len, &state);
			add_assoc_zval_ex(cursor, ZEND_STRS("fields"), state.zchild);
		}
		{
			zval *read_preference = NULL;

			MAKE_STD_ZVAL(read_preference);
			php_phongo_read_preference_to_zval(read_preference, result->cursor->read_prefs);
			add_assoc_zval_ex(cursor, ZEND_STRS("read_preference"), read_preference);
		}

#define _ADD_INT(z, field) add_assoc_long_ex(z, ZEND_STRS(#field), result->cursor->field)
		_ADD_INT(cursor, flags);
		_ADD_INT(cursor, skip);
		_ADD_INT(cursor, limit);
		_ADD_INT(cursor, count);
		_ADD_INT(cursor, batch_size);
#undef _ADD_INT

		add_assoc_string_ex(cursor, ZEND_STRS("ns"), result->cursor->ns, 1);
		if (result->cursor->current) {
			php_phongo_bson_state  state = PHONGO_BSON_STATE_INITIALIZER;

			MAKE_STD_ZVAL(state.zchild);
			bson_to_zval(bson_get_data(result->cursor->current), result->cursor->current->len, &state);
			add_assoc_zval_ex(cursor, ZEND_STRS("current_doc"), state.zchild);
		}
		add_assoc_zval_ex(retval, ZEND_STRS("cursor"), cursor);
	} else {
		add_assoc_null_ex(retval, ZEND_STRS("cursor"));
	}

	if (result->firstBatch) {
		php_phongo_bson_state  state = PHONGO_BSON_STATE_INITIALIZER;

		MAKE_STD_ZVAL(state.zchild);
		bson_to_zval(bson_get_data(result->firstBatch), result->firstBatch->len, &state);
		add_assoc_zval_ex(retval, ZEND_STRS("firstBatch"), state.zchild);
	} else {
		add_assoc_null_ex(retval, ZEND_STRS("firstBatch"));
	}
	add_assoc_long_ex(retval, ZEND_STRS("server_id"), result->server_id);
	add_assoc_bool_ex(retval, ZEND_STRS("is_command_cursor"), result->is_command_cursor);

} /* }}} */


mongoc_client_t *php_phongo_make_mongo_client(const char *uri, zval *driverOptions TSRMLS_DC) /* {{{ */
{
	php_stream_context       *ctx = NULL;
	mongoc_client_t *client = mongoc_client_new(uri);

	if (!client) {
		return false;
	}

	if (driverOptions) {
		zval **tmp;

		if (zend_hash_find(Z_ARRVAL_P(driverOptions), "context", strlen("context") + 1, (void**)&tmp) == SUCCESS) {
			ctx = php_stream_context_from_zval(*tmp, PHP_FILE_NO_DEFAULT_CONTEXT);
		} else if (FG(default_context)) {
			ctx = FG(default_context);
		}

		if (ctx) {
			const mongoc_uri_t *muri = mongoc_client_get_uri(client);
			const char *mech = mongoc_uri_get_auth_mechanism(muri);

			/* Check if we are doing X509 auth, in which case extract the username (subject) from the cert if no username is provided */
			if (mech && !strcasecmp(mech, "MONGODB-X509") && !mongoc_uri_get_username(muri)) {
				zval **pem;

				if (SUCCESS == php_stream_context_get_option(ctx, "ssl", "local_cert", &pem)) {
					char filename[MAXPATHLEN];

					convert_to_string_ex(pem);
					if (VCWD_REALPATH(Z_STRVAL_PP(pem), filename)) {
						mongoc_ssl_opt_t  ssl_options;

						ssl_options.pem_file = filename;
						mongoc_client_set_ssl_opts(client, &ssl_options);
					}
				}
			}
		}

		if (zend_hash_find(Z_ARRVAL_P(driverOptions), "debug", strlen("debug") + 1, (void**)&tmp) == SUCCESS) {
			convert_to_string(*tmp);

			zend_alter_ini_entry_ex((char *)"phongo.debug_log", sizeof("phongo.debug_log") , Z_STRVAL_PP(tmp), Z_STRLEN_PP(tmp), PHP_INI_USER, PHP_INI_STAGE_RUNTIME, 0 TSRMLS_CC);
		}
	}

	mongoc_client_set_stream_initiator(client, phongo_stream_initiator, ctx);

	return client;
} /* }}} */

void php_phongo_new_utcdatetime_from_epoch(zval *object, int64_t msec_since_epoch TSRMLS_DC) /* {{{ */
{
	php_phongo_utcdatetime_t     *intern;

	object_init_ex(object, php_phongo_utcdatetime_ce);

	intern = (php_phongo_utcdatetime_t *)zend_object_store_get_object(object TSRMLS_CC);
	intern->milliseconds = msec_since_epoch;
} /* }}} */

void php_phongo_new_datetime_from_utcdatetime(zval *object, int64_t milliseconds TSRMLS_DC) /* {{{ */
{
	php_date_obj             *datetime_obj;
	char                     *sec;
	int                       sec_len;

	object_init_ex(object, php_date_get_date_ce());

#ifdef WIN32
	sec_len = spprintf(&sec, 0, "@%I64d", (int64_t) milliseconds / 1000);
#else
	sec_len = spprintf(&sec, 0, "@%lld", (long long int) milliseconds / 1000);
#endif

	datetime_obj = zend_object_store_get_object(object TSRMLS_CC);
	php_date_initialize(datetime_obj, sec, sec_len, NULL, NULL, 0 TSRMLS_CC);
	efree(sec);
	datetime_obj->time->f = milliseconds % 1000;
} /* }}} */
void php_phongo_new_timestamp_from_increment_and_timestamp(zval *object, int32_t increment, int32_t timestamp TSRMLS_DC) /* {{{ */
{
	php_phongo_timestamp_t     *intern;

	object_init_ex(object, php_phongo_timestamp_ce);

	intern = (php_phongo_timestamp_t *)zend_object_store_get_object(object TSRMLS_CC);
	intern->increment = increment;
	intern->timestamp = timestamp;
} /* }}} */
void php_phongo_new_javascript_from_javascript(zval *object, const char *code, size_t code_len TSRMLS_DC) /* {{{ */
{
	php_phongo_javascript_t     *intern;

	object_init_ex(object, php_phongo_javascript_ce);

	intern = (php_phongo_javascript_t *)zend_object_store_get_object(object TSRMLS_CC);
	intern->javascript = estrndup(code, code_len);
	intern->javascript_len = code_len;
} /* }}} */
void php_phongo_new_javascript_from_javascript_and_scope(zval *object, const char *code, size_t code_len, const bson_t *scope TSRMLS_DC) /* {{{ */
{
	php_phongo_javascript_t     *intern;

	object_init_ex(object, php_phongo_javascript_ce);

	intern = (php_phongo_javascript_t *)zend_object_store_get_object(object TSRMLS_CC);
	intern->javascript = estrndup(code, code_len);
	intern->javascript_len = code_len;
	intern->document = bson_copy(scope);
} /* }}} */
void php_phongo_new_binary_from_binary_and_subtype(zval *object, const char *data, size_t data_len, bson_subtype_t type TSRMLS_DC) /* {{{ */
{
	php_phongo_binary_t     *intern;

	object_init_ex(object, php_phongo_binary_ce);

	intern = (php_phongo_binary_t *)zend_object_store_get_object(object TSRMLS_CC);
	intern->data = estrndup(data, data_len);
	intern->data_len = data_len;
	intern->subtype = type;
} /* }}} */
void php_phongo_new_regex_from_regex_and_options(zval *object, const char *pattern, const char *flags TSRMLS_DC) /* {{{ */
{
	php_phongo_regex_t     *intern;

	object_init_ex(object, php_phongo_regex_ce);

	intern = (php_phongo_regex_t *)zend_object_store_get_object(object TSRMLS_CC);
	intern->pattern_len = strlen(pattern);
	intern->pattern = estrndup(pattern, intern->pattern_len);
	intern->flags_len = strlen(flags);
	intern->flags = estrndup(flags, intern->flags_len);
} /* }}} */

bool php_phongo_writeresult_get_write_errors(php_phongo_writeresult_t *writeresult, bson_error_t *error)
{
	const char *err = NULL;
	uint32_t code = 0;

	bson_iter_t iter;
	bson_iter_t citer;
	if (!bson_empty0 (&writeresult->write_result.writeErrors) &&
			bson_iter_init (&iter, &writeresult->write_result.writeErrors) &&
			bson_iter_next (&iter) &&
			BSON_ITER_HOLDS_DOCUMENT (&iter) &&
			bson_iter_recurse (&iter, &citer)) {
		while (bson_iter_next (&citer)) {
			if (BSON_ITER_IS_KEY (&citer, "errmsg")) {
				err = bson_iter_utf8 (&citer, NULL);
			} else if (BSON_ITER_IS_KEY (&citer, "code")) {
				code = bson_iter_int32 (&citer);
			}
		}

		bson_set_error(error, PHONGO_ERROR_WRITE_SINGLE_FAILED, code, "%s", err);
		return true;
	}
	return false;
}
bool php_phongo_writeresult_get_writeconcern_error(php_phongo_writeresult_t *writeresult, bson_error_t *error)
{
	const char *err = NULL;
	uint32_t code = 0;

	if (!bson_empty0(&writeresult->write_result.writeConcernError)) {
		bson_iter_t iter;

		if (bson_iter_init_find(&iter, &writeresult->write_result.writeConcernError, "code") && BSON_ITER_HOLDS_INT32(&iter)) {
			code = bson_iter_int32(&iter);
		}
		if (bson_iter_init_find(&iter, &writeresult->write_result.writeConcernError, "errmsg") && BSON_ITER_HOLDS_UTF8(&iter)) {
			err = bson_iter_utf8(&iter, NULL);
		}

		bson_set_error(error, PHONGO_ERROR_WRITECONCERN_FAILED, code, "%s", err);
		return true;
	}

	return false;
}
zval* php_phongo_throw_write_errors(php_phongo_writeresult_t *wr TSRMLS_DC)
{
	bson_error_t error;

	if (php_phongo_writeresult_get_write_errors(wr, &error)) {
		return phongo_throw_exception(PHONGO_ERROR_WRITE_SINGLE_FAILED TSRMLS_CC, "%s", error.message);
	}
	return NULL;
}
zval* php_phongo_throw_write_concern_error(php_phongo_writeresult_t *wr TSRMLS_DC)
{
	bson_error_t error;

	if (php_phongo_writeresult_get_writeconcern_error(wr, &error)) {
		return phongo_throw_exception(PHONGO_ERROR_WRITECONCERN_FAILED TSRMLS_CC, "%s", error.message);
	}
	return NULL;
}
php_phongo_writeresult_t *php_phongo_writeresult_get_from_bulkwriteexception(zval *ex TSRMLS_DC)
{
	zval *wr = zend_read_property(php_phongo_bulkwriteexception_ce, ex, ZEND_STRL("writeResult"), 0 TSRMLS_CC);

	return (php_phongo_writeresult_t *)zend_object_store_get_object(wr TSRMLS_CC);
}

void php_phongo_result_free(php_phongo_result_t *result)
{
	if (result->firstBatch) {
		bson_clear(&result->firstBatch);
		result->firstBatch = NULL;
	}
	if (result->cursor) {
		mongoc_cursor_destroy(result->cursor);
		result->cursor = NULL;
	}
	if (result->visitor_data.zchild) {
		zval_ptr_dtor(&result->visitor_data.zchild);
		result->visitor_data.zchild = NULL;
	}
}

/* {{{ Iterator */
static void phongo_result_iterator_invalidate_current(zend_object_iterator *iter TSRMLS_DC) /* {{{ */
{
	php_phongo_result_t *result = NULL;

	result = ((phongo_cursor_it *)iter)->iterator.data;
	if (result->visitor_data.zchild) {
		zval_ptr_dtor(&result->visitor_data.zchild);
		result->visitor_data.zchild = NULL;
	}
} /* }}} */

static void phongo_result_iterator_dtor(zend_object_iterator *iter TSRMLS_DC) /* {{{ */
{
	efree(iter);
} /* }}} */

static int phongo_result_iterator_valid(zend_object_iterator *iter TSRMLS_DC) /* {{{ */
{
	php_phongo_result_t *result = NULL;

	result = ((phongo_cursor_it *)iter)->iterator.data;

	if (result->visitor_data.zchild) {
		return SUCCESS;
	}

	return FAILURE;
} /* }}} */

static void phongo_result_iterator_get_current_key(zend_object_iterator *iter, zval *key TSRMLS_DC) /* {{{ */
{
	ZVAL_LONG(key, ((phongo_cursor_it *)iter)->current);
} /* }}} */

static void phongo_result_iterator_get_current_data(zend_object_iterator *iter, zval ***data TSRMLS_DC) /* {{{ */
{
	php_phongo_result_t *result = NULL;

	result = ((phongo_cursor_it *)iter)->iterator.data;

	*data = &result->visitor_data.zchild;
} /* }}} */

static void phongo_result_iterator_move_forward(zend_object_iterator *iter TSRMLS_DC) /* {{{ */
{
	php_phongo_result_t   *result = NULL;
	phongo_cursor_it      *cursor_it = (phongo_cursor_it *)iter;
	const bson_t          *doc;

	result = ((phongo_cursor_it *)iter)->iterator.data;
	iter->funcs->invalidate_current(iter TSRMLS_CC);

	((phongo_cursor_it *)iter)->current++;
	if (bson_iter_next(&cursor_it->first_batch_iter)) {
		if (BSON_ITER_HOLDS_DOCUMENT (&cursor_it->first_batch_iter)) {
			const uint8_t *data = NULL;
			uint32_t data_len = 0;

			bson_iter_document(&cursor_it->first_batch_iter, &data_len, &data);

			MAKE_STD_ZVAL(result->visitor_data.zchild);
			bson_to_zval(data, data_len, &result->visitor_data);
			return;
		}
	}
	if (mongoc_cursor_next(result->cursor, &doc)) {
		MAKE_STD_ZVAL(result->visitor_data.zchild);
		bson_to_zval(bson_get_data(doc), doc->len, &result->visitor_data);
	} else {
		iter->funcs->invalidate_current(iter TSRMLS_CC);
	}

} /* }}} */

static void phongo_result_iterator_rewind(zend_object_iterator *iter TSRMLS_DC) /* {{{ */
{
	php_phongo_result_t *result = NULL;
	phongo_cursor_it    *cursor_it = (phongo_cursor_it *)iter;

	result = ((phongo_cursor_it *)iter)->iterator.data;

	iter->funcs->invalidate_current(iter TSRMLS_CC);
	((phongo_cursor_it *)iter)->current = 0;

	/* firstBatch is empty when the query simply didn't return any results */
	if (result->firstBatch) {
		if (result->is_command_cursor) {
			if (!bson_iter_init(&cursor_it->first_batch_iter, result->firstBatch)) {
				return;
			}
			if (bson_iter_next (&cursor_it->first_batch_iter)) {
				if (BSON_ITER_HOLDS_DOCUMENT (&cursor_it->first_batch_iter)) {
					const uint8_t *data = NULL;
					uint32_t data_len = 0;

					bson_iter_document(&cursor_it->first_batch_iter, &data_len, &data);
					MAKE_STD_ZVAL(result->visitor_data.zchild);
					bson_to_zval(data, data_len, &result->visitor_data);
				}
			}
		} else {
			MAKE_STD_ZVAL(result->visitor_data.zchild);
			bson_to_zval(bson_get_data(result->firstBatch), result->firstBatch->len, &result->visitor_data);
		}
	}
} /* }}} */

/* iterator handler table */
zend_object_iterator_funcs phongo_result_iterator_funcs = {
	phongo_result_iterator_dtor,
	phongo_result_iterator_valid,
	phongo_result_iterator_get_current_data,
	phongo_result_iterator_get_current_key,
	phongo_result_iterator_move_forward,
	phongo_result_iterator_rewind,
	phongo_result_iterator_invalidate_current
};
zend_object_iterator_funcs zend_interface_iterator_funcs_iterator_default = {
	phongo_result_iterator_dtor,
	zend_user_it_valid,
	zend_user_it_get_current_data,
	zend_user_it_get_current_key,
	zend_user_it_move_forward,
	zend_user_it_rewind,
	zend_user_it_invalidate_current
};

zend_object_iterator *phongo_cursor_get_iterator(zend_class_entry *ce, zval *object, int by_ref TSRMLS_DC) /* {{{ */
{
	php_phongo_cursor_t    *intern = (php_phongo_cursor_t *)zend_object_store_get_object(object TSRMLS_CC);
	phongo_cursor_it       *cursor_it = NULL;

	if (by_ref) {
		zend_error(E_ERROR, "An iterator cannot be used with foreach by reference");
	}


	cursor_it = ecalloc(1, sizeof(phongo_cursor_it));

	cursor_it->iterator.data  = intern->result;
	cursor_it->iterator.funcs = &phongo_result_iterator_funcs;

	return (zend_object_iterator*)cursor_it;
} /* }}} */
zend_object_iterator *phongo_result_get_iterator(zend_class_entry *ce, zval *object, int by_ref TSRMLS_DC) /* {{{ */
{
	php_phongo_result_t    *result = (php_phongo_result_t *)zend_object_store_get_object(object TSRMLS_CC);

	if (by_ref) {
		zend_error(E_ERROR, "An iterator cannot be used with foreach by reference");
	}

	/* If we have a custom iterator */
	if (result->ce_get_iterator != NULL) {
		zend_user_iterator *iterator = NULL;

		zval_ptr_dtor(&object);
		object_init_ex(object, result->ce_get_iterator);

		iterator = emalloc(sizeof(zend_user_iterator));

		Z_ADDREF_P(object);
		iterator->it.data = (void*)object;
		iterator->it.funcs = &zend_interface_iterator_funcs_iterator_default;
		iterator->ce = Z_OBJCE_P(object);
		iterator->value = NULL;
		return (zend_object_iterator*)iterator;
	} else {
		phongo_cursor_it       *cursor_it = NULL;

		cursor_it = ecalloc(1, sizeof(phongo_cursor_it));

		if (result->visitor_data.zchild) {
			zval_ptr_dtor(&result->visitor_data.zchild);
			result->visitor_data.zchild = NULL;
		}
		cursor_it->iterator.data  = (void*)result;
		cursor_it->iterator.funcs = &phongo_result_iterator_funcs;

		return (zend_object_iterator*)cursor_it;
	}
} /* }}} */
/* }}} */


/* {{{ Memory allocation wrappers */
static void* php_phongo_malloc(size_t num_bytes) /* {{{ */
{
	return emalloc(num_bytes);
} /* }}} */

static void* php_phongo_calloc(size_t num_members, size_t num_bytes) /* {{{ */
{
	return ecalloc(num_members, num_bytes);
} /* }}} */

static void* php_phongo_realloc(void *mem, size_t num_bytes) { /* {{{ */
	return erealloc(mem, num_bytes);
} /* }}} */

static void php_phongo_free(void *mem) /* {{{ */
{
	if (mem) {
		return efree(mem);
	}
} /* }}} */

/* }}} */

#ifdef PHP_DEBUG
void _phongo_debug_bson(bson_t *bson)
{
	char   *str;
	size_t  str_len;

	str = bson_as_json(bson, &str_len);

	php_printf("JSON: %s\n", str);
}
#endif

/* {{{ M[INIT|SHUTDOWN] R[INIT|SHUTDOWN] G[INIT|SHUTDOWN] MINFO INI */

/* {{{ INI entries */
PHP_INI_BEGIN()
	STD_PHP_INI_ENTRY("mongodb.debug_log", "", PHP_INI_ALL, OnUpdateString, debug_log, zend_mongodb_globals, mongodb_globals)
PHP_INI_END()
/* }}} */

/* {{{ PHP_GINIT_FUNCTION */
PHP_GINIT_FUNCTION(mongodb)
{
	bson_mem_vtable_t bsonMemVTable = {
		php_phongo_malloc,
		php_phongo_calloc,
		php_phongo_realloc,
		php_phongo_free,
	};
	mongodb_globals->debug_log = NULL;
	mongodb_globals->bsonMemVTable = bsonMemVTable;

}
/* }}} */

/* {{{ PHP_MINIT_FUNCTION */
PHP_MINIT_FUNCTION(mongodb)
{
	void ***ctx = NULL;
	TSRMLS_SET_CTX(ctx);
	(void)type; /* We don't care if we are loaded via dl() or extension= */


	REGISTER_INI_ENTRIES();

	/* Initialize libmongoc */
	mongoc_init();
	/* Initialize libbson */
	bson_mem_set_vtable(&MONGODB_G(bsonMemVTable));
	mongoc_log_set_handler(php_phongo_log, ctx);

	/* Prep default object handlers to be used when we register the classes */
	memcpy(&phongo_std_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	phongo_std_object_handlers.clone_obj            = NULL;
	/*
	phongo_std_object_handlers.get_debug_info       = NULL;
	phongo_std_object_handlers.compare_objects      = NULL;
	phongo_std_object_handlers.cast_object          = NULL;
	phongo_std_object_handlers.count_elements       = NULL;
	phongo_std_object_handlers.get_closure          = NULL;
	*/

	PHP_MINIT(bson)(INIT_FUNC_ARGS_PASSTHRU);

	PHP_MINIT(Command)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(Cursor)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(CursorId)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(Manager)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(Query)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(ReadPreference)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(Result)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(Server)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(BulkWrite)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(WriteConcern)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(WriteConcernError)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(WriteError)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(WriteResult)(INIT_FUNC_ARGS_PASSTHRU);

	PHP_MINIT(Exception)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(RuntimeException)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(InvalidArgumentException)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(ConnectionException)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(AuthenticationException)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(SSLConnectionException)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(WriteException)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(WriteConcernException)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(BulkWriteException)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(DuplicateKeyException)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(ExecutionTimeoutException)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(ConnectionTimeoutException)(INIT_FUNC_ARGS_PASSTHRU);

	PHP_MINIT(Type)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(Serializable)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(Unserializable)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(Persistable)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(Binary)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(Javascript)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(MaxKey)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(MinKey)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(ObjectID)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(Regex)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(Timestamp)(INIT_FUNC_ARGS_PASSTHRU);
	PHP_MINIT(UTCDatetime)(INIT_FUNC_ARGS_PASSTHRU);

	REGISTER_STRING_CONSTANT("MONGODB_VERSION", (char *)MONGODB_VERSION_S, CONST_CS | CONST_PERSISTENT);
	REGISTER_STRING_CONSTANT("MONGODB_STABILITY", (char *)MONGODB_STABILITY_S, CONST_CS | CONST_PERSISTENT);

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_RINIT_FUNCTION */
PHP_RINIT_FUNCTION(mongodb)
{
	(void)type; /* We don't care if we are loaded via dl() or extension= */
	(void)module_number; /* Really doesn't matter which module number we are */

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_RSHUTDOWN_FUNCTION */
PHP_RSHUTDOWN_FUNCTION(mongodb)
{
	(void)type; /* We don't care if we are loaded via dl() or extension= */
	(void)module_number; /* Really doesn't matter which module number we are */

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION */
PHP_MSHUTDOWN_FUNCTION(mongodb)
{
	(void)type; /* We don't care if we are loaded via dl() or extension= */

	bson_mem_restore_vtable();
	/* Cleanup after libmongoc */
	mongoc_cleanup();

	UNREGISTER_INI_ENTRIES();

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_GSHUTDOWN_FUNCTION */
PHP_GSHUTDOWN_FUNCTION(mongodb)
{
	mongodb_globals->debug_log = NULL;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION */
PHP_MINFO_FUNCTION(mongodb)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "mongodb support", "enabled");
	php_info_print_table_row(2, "mongodb version", MONGODB_VERSION_S);
	php_info_print_table_row(2, "mongodb stability", MONGODB_STABILITY_S);
	php_info_print_table_row(2, "libmongoc version", MONGOC_VERSION_S);
	php_info_print_table_row(2, "libbson version", BSON_VERSION_S);
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}
/* }}} */
/* }}} */


/* {{{ mongodb_functions[]
*/
ZEND_BEGIN_ARG_INFO_EX(ai_bson_fromArray, 0, 0, 1)
	ZEND_ARG_INFO(0, array)
ZEND_END_ARG_INFO();

ZEND_BEGIN_ARG_INFO_EX(ai_bson_toArray, 0, 0, 1)
	ZEND_ARG_INFO(0, bson)
ZEND_END_ARG_INFO();

ZEND_BEGIN_ARG_INFO_EX(ai_bson_toJSON, 0, 0, 1)
	ZEND_ARG_INFO(0, bson)
ZEND_END_ARG_INFO();

ZEND_BEGIN_ARG_INFO_EX(ai_bson_fromJSON, 0, 0, 1)
	ZEND_ARG_INFO(0, json)
ZEND_END_ARG_INFO();

const zend_function_entry mongodb_functions[] = {
	ZEND_NS_FE("BSON", fromArray, ai_bson_fromArray)
	ZEND_NS_FE("BSON", toArray,   ai_bson_toArray)
	ZEND_NS_FE("BSON", toJSON,    ai_bson_toJSON)
	ZEND_NS_FE("BSON", fromJSON,  ai_bson_fromJSON)
	PHP_FE_END
};
/* }}} */

/* {{{ mongodb_module_entry
 */
zend_module_entry mongodb_module_entry = {
	STANDARD_MODULE_HEADER,
	"mongodb",
	mongodb_functions,
	PHP_MINIT(mongodb),
	PHP_MSHUTDOWN(mongodb),
	PHP_RINIT(mongodb),
	PHP_RSHUTDOWN(mongodb),
	PHP_MINFO(mongodb),
	MONGODB_VERSION,
	PHP_MODULE_GLOBALS(mongodb),
	PHP_GINIT(mongodb),
	PHP_GSHUTDOWN(mongodb),
	NULL,
	STANDARD_MODULE_PROPERTIES_EX
};
/* }}} */

#ifdef COMPILE_DL_MONGODB
ZEND_GET_MODULE(mongodb)
#endif

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
