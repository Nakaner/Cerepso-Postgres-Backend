/*
 * table.hpp
 *
 *  Created on: 23.06.2016
 *      Author: michael
 */

#ifndef TABLE_HPP_
#define TABLE_HPP_

#include <libpq-fe.h>
#include <boost/format.hpp>
#include "columns.hpp"
#include <sstream>
#include <osmium/osm/types.hpp>
#include <geos/geom/Point.h>
#include <string.h>
#include <sstream>
#include <boost/iostreams/device/file.hpp>
#include <boost/iostreams/stream.hpp>
#include <geos/io/WKBReader.h>
#include <geos/geom/Coordinate.h>

namespace postgres_drivers {

    /**
     * This class manages connection to a database table. We have one connection per table,
     * therefore this class is called Table, not DBConnection.
     */

    class Table {
    protected:
        /**
         * name of the table
         */
        std::string m_name = "";

        /**
         * configuration (especially name of the database to connect to)
         */
        Config& m_config;

        /**
         * track if COPY mode has been entered
         */
        bool m_copy_mode = false;

        /**
         * track if a BEGIN COMMIT block has been opened
         */
        bool m_begin = false;

        Columns& m_columns;

        /**
         * connection to database
         *
         * This pointer is a nullpointer if this table is used in demo mode (for testing purposes).
         */
        PGconn *m_database_connection;

        /**
         * maximum size of copy buffer
         */
        static const int BUFFER_SEND_SIZE = 10000;

        /**
         * create all necessary prepared statements for this table
         *
         * This method chooses the suitable prepared statements which are dependend from the table type (point vs. way vs. …).
         */
        void create_prepared_statements() {
            // create delete statement
            std::string query = (boost::format("DELETE FROM %1% WHERE osm_id = $1") % m_name).str();
            create_prepared_statement("delete_statement", query, 1);
            if (m_columns.get_type() == TableType::POINT || m_columns.get_type() == TableType::UNTAGGED_POINT) {
                query = (boost::format("SELECT ST_X(geom), ST_Y(geom) FROM %1% WHERE osm_id = $1") % m_name).str();
                create_prepared_statement("get_point", query, 1);
            } else if (m_columns.get_type() == TableType::WAYS_LINEAR) {
                query = (boost::format("SELECT geom FROM %1% WHERE osm_id = $1") % m_name).str();
                create_prepared_statement("get_linestring", query, 1);
            }
        }

        /**
         * create a prepared statement
         */
        void create_prepared_statement(const char* name, std::string query, int params_count) {
            PGresult *result = PQprepare(m_database_connection, name, query.c_str(), params_count, NULL);
            if (PQresultStatus(result) != PGRES_COMMAND_OK) {
                PQclear(result);
                throw std::runtime_error((boost::format("%1% failed: %2%\n") % query % PQerrorMessage(m_database_connection)).str());
            }
            PQclear(result);
        }

        /**
         * get ID of geometry column, first column is 0
         */
        int get_geometry_column_id();

    public:
        Table() = delete;

        /**
         * constructor for production, establishes database connection, read-only access to the database
         */
        /// \todo replace const char* by std::string&
        Table(const char* table_name, Config& config, Columns& columns) :
                m_name(table_name),
                m_config(config),
                m_copy_mode(false),
                m_columns(columns) {
            std::string connection_params = "dbname=";
            connection_params.append(m_config.m_database_name);
            m_database_connection = PQconnectdb(connection_params.c_str());
            if (PQstatus(m_database_connection) != CONNECTION_OK) {
                throw std::runtime_error((boost::format("Cannot establish connection to database: %1%\n")
                    %  PQerrorMessage(m_database_connection)).str());
            }
        }

        /**
         * constructor for testing, does not establishes database connection
         */
        Table(Config& config, Columns& columns) :
                m_name(""),
                m_config(config),
                m_copy_mode(false),
                m_columns(columns) { }

        ~Table() {
            if (m_name != "") {
                if (m_copy_mode) {
                    end_copy();
                }
                if (m_begin) {
                    commit();
                }
                PQfinish(m_database_connection);
            }
        }


        Columns& get_columns() {
            return m_columns;
        }

        /**
         * escape a string which should be inserted into a hstore column
         *
         * @param source string which should be escaped
         * @param destination string where the escaped string has to be appended (later usually used for INSERT query)
         */
        static void escape4hstore(const char* source, std::string& destination) {
            /**
            * taken from osm2pgsql/table.cpp, void table_t::escape4hstore(const char *src, string& dst)
            */
            destination.push_back('"');
            for (size_t i = 0; i < strlen(source); ++i) {
                switch (source[i]) {
                    case '\\':
                        destination.append("\\\\\\\\");
                        break;
                    case '"':
                        destination.append("\\\\\"");
                        break;
                    case '\t':
                        destination.append("\\\t");
                        break;
                    case '\r':
                        destination.append("\\\r");
                        break;
                    case '\n':
                        destination.append("\\\n");
                        break;
                    default:
                        destination.push_back(source[i]);
                        break;
                }
            }
            destination.push_back('"');
        }

        /**
         * escape a string which should be inserted into a non-hstore column which is inserted into the database using SQL COPY
         *
         * @param source string which should be escaped
         * @param destination string where the escaped string has to be appended (later usually used for INSERT query)
         */
        static void escape(const char* source, std::string& destination) {
            /**
            * copied (and modified) from osm2pgsql/pgsql.cpp, void escape(const std::string &src, std::string &dst)
            */
            for (size_t i = 0; i < strlen(source); ++i) {
                switch(source[i]) {
                    case '\\':
                        destination.append("\\\\");
                        break;
                    case 8:
                        destination.append("\\\b");
                        break;
                    case 12:
                        destination.append("\\\f");
                        break;
                    case '\n':
                        destination.append("\\\n");
                        break;
                    case '\r':
                        destination.append("\\\r");
                        break;
                    case '\t':
                        destination.append("\\\t");
                        break;
                    case 11:
                        destination.append("\\\v");
                        break;
                    default:
                        destination.push_back(source[i]);
                        break;
                }
            }
        }

        /**
         * check if the object mentioned by this query exists and, if yes, delete it.
         *
         * @param query SQL query
         */
        void delete_if_existing(const char* database_query);

        /**
         * delete (try it) all objects with the given OSM object IDs
         */
        void delete_from_list(std::vector<osmium::object_id_type>& list) {
            assert(!m_copy_mode);
            for (osmium::object_id_type id : list) {
                PGresult *result = PQexec(m_database_connection, (boost::format("DELETE FROM %1% WHERE osm_id = %2%") % m_name % id).str().c_str());
                if (PQresultStatus(result) != PGRES_COMMAND_OK) {
                    PQclear(result);
                    throw std::runtime_error((boost::format("%1% failed: %2%\n") % (boost::format("DELETE FROM %1% WHERE osm_id = %2%") % m_name % id).str().c_str() % PQerrorMessage(m_database_connection)).str());
                }
                PQclear(result);
            }
        }

        /**
         * delete an object with given ID
         */
        void delete_object(const osmium::object_id_type id) {
            assert(!m_copy_mode);
            char const *paramValues[1];
            char buffer[64];
            sprintf(buffer, "%ld", id);
            paramValues[0] = buffer;
            PGresult *result = PQexecPrepared(m_database_connection, "delete_statement", 1, paramValues, nullptr, nullptr, 0);
            if (PQresultStatus(result) != PGRES_COMMAND_OK) {
                PQclear(result);
                throw std::runtime_error((boost::format("Deleting object %1% from %2% failed: %3%\n") % id % m_name % PQresultErrorMessage(result)).str());
            }
            PQclear(result);
        }

        /**
         * send a line to the database (it will get it from STDIN) during copy mode
         */
        void send_line(const std::string& line) {
            if (!m_database_connection) {
                return;
            }
            if (!m_copy_mode) {
                throw std::runtime_error((boost::format("Insertion via COPY \"%1%\" failed: You are not in COPY mode!\n") % line).str());
            }
            if (line[line.size()-1] != '\n') {
                throw std::runtime_error((boost::format("Insertion via COPY into %1% failed: Line does not end with \\n\n%2%") % m_name % line).str());
            }
            if (PQputCopyData(m_database_connection, line.c_str(), line.size()) != 1) {
                throw std::runtime_error((boost::format("Insertion via COPY \"%1%\" failed: %2%\n") % line % PQerrorMessage(m_database_connection)).str());
            }
        }


        std::string& get_name() {
            return m_name;
        }

        /**
         * start COPY mode
         */
        void start_copy() {
            if (!m_database_connection) {
                std::runtime_error("No database connection.");
            }
            std::string copy_command = "COPY ";
            copy_command.append(m_name);
            copy_command.append(" (");
            for (ColumnsIterator it = m_columns.begin(); it != m_columns.end(); it++) {
                copy_command.append(it->first);
                copy_command.push_back(',');
            }
            copy_command.pop_back();
            copy_command.append(") FROM STDIN");
            PGresult *result = PQexec(m_database_connection, copy_command.c_str());
            if (PQresultStatus(result) != PGRES_COPY_IN) {
                PQclear(result);
                throw std::runtime_error((boost::format("%1% failed: %2%\n") % copy_command % PQerrorMessage(m_database_connection)).str());
            }
            PQclear(result);
            m_copy_mode = true;
        }

        /**
         * stop COPY mode
         */
        void end_copy() {
            if (!m_database_connection) {
                return;
            }
            if (PQputCopyEnd(m_database_connection, nullptr) != 1) {
                throw std::runtime_error(PQerrorMessage(m_database_connection));
            }
            PGresult *result = PQgetResult(m_database_connection);
            if (PQresultStatus(result) != PGRES_COMMAND_OK) {
                throw std::runtime_error((boost::format("COPY END command failed: %1%\n") %  PQerrorMessage(m_database_connection)).str());
                PQclear(result);
            }
            m_copy_mode = false;
            PQclear(result);
        }

        /**
         * get current state of connection
         * Is it in copy mode or not?
         */
        bool get_copy() {
            return m_copy_mode;
        }

        /**
         * send BEGIN to table
         *
         * This method can be called both from inside the class and from outside (if you want to get your changes persistend after
         * you sent them all to the database.
         */
        void send_begin() {
            send_query("BEGIN");
            m_begin = true;
        }

        /*
         * send COMMIT to table
         *
         * This method is intended to be called from the destructor of this class.
         */
        void commit() {
            send_query("COMMIT");
            m_begin = false;
        }

        /**
         * Send any SQL query.
         *
         * This query will not return anything, i.e. it is useful for INSERT and DELETE operations.
         */
        void send_query(const char* query) {
            if (!m_database_connection) {
                return;
            }
            if (m_copy_mode) {
                throw std::runtime_error((boost::format("%1% failed: You are in COPY mode.\n") % query % PQerrorMessage(m_database_connection)).str());
            }
            PGresult *result = PQexec(m_database_connection, query);
            if (PQresultStatus(result) != PGRES_COMMAND_OK) {
                PQclear(result);
                throw std::runtime_error((boost::format("%1% failed: %2%\n") % query % PQerrorMessage(m_database_connection)).str());
            }
            PQclear(result);
        }

        /*
         * send COMMIT to table and checks if this is currently allowed (i.e. currently not in COPY mode)
         *
         * This method is intended to be called from outside of this class and therefore contains some additional checks.
         * It will send an additional BEGIN after sending COMMIT.
         */
        void intermediate_commit() {
            if (m_copy_mode) {
                throw std::runtime_error((boost::format("secure COMMIT failed: You are in COPY mode.\n")).str());
            }
            assert(m_begin);
            commit();
        }

        /**
         * get the longitude and latitude of a node
         *
         * @param id OSM ID
         *
         * @returns unique_ptr to coordinate or empty unique_ptr (equivalent to nullptr if it were a raw pointer) otherwise
         */
        std::unique_ptr<geos::geom::Coordinate> get_point(const osmium::object_id_type id) {
            assert(m_database_connection);
            assert(!m_copy_mode);
        //    assert(!m_begin);
            std::unique_ptr<geos::geom::Coordinate> coord;
            char const *paramValues[1];
            char buffer[64];
            sprintf(buffer, "%ld", id);
            paramValues[0] = buffer;
            PGresult *result = PQexecPrepared(m_database_connection, "get_point", 1, paramValues, nullptr, nullptr, 0);
            if ((PQresultStatus(result) != PGRES_COMMAND_OK) && (PQresultStatus(result) != PGRES_TUPLES_OK)) {
                throw std::runtime_error((boost::format("Failed: %1%\n") % PQresultErrorMessage(result)).str());
                PQclear(result);
                return coord;
            }
            if (PQntuples(result) == 0) {
        //        throw std::runtime_error(((boost::format("Node %1% not found. ") % id)).str());
                PQclear(result);
                return coord;
            }
            coord = std::unique_ptr<geos::geom::Coordinate>(new geos::geom::Coordinate(atof(PQgetvalue(result, 0, 0)), atof(PQgetvalue(result, 0, 1))));
            PQclear(result);
            return coord;
        }

        /**
         * get a way as GEOS LineString
         *
         * @param id OSM ID
         *
         * @returns unique_ptr to Geometry or empty unique_ptr otherwise
         */
        std::unique_ptr<geos::geom::Geometry> get_linestring(const osmium::object_id_type id, geos::geom::GeometryFactory& geometry_factory) {
            assert(m_database_connection);
            assert(!m_copy_mode);
            std::unique_ptr<geos::geom::Geometry> linestring;
            char const *paramValues[1];
            char buffer[64];
            sprintf(buffer, "%ld", id);
            paramValues[0] = buffer;
            PGresult *result = PQexecPrepared(m_database_connection, "get_linestring", 1, paramValues, nullptr, nullptr, 0);
            if ((PQresultStatus(result) != PGRES_COMMAND_OK) && (PQresultStatus(result) != PGRES_TUPLES_OK)) {
                throw std::runtime_error((boost::format("Failed: %1%\n") % PQresultErrorMessage(result)).str());
                PQclear(result);
                return linestring;
            }
            if (PQntuples(result) == 0) {
                PQclear(result);
                return linestring;
            }
            std::stringstream geom_stream;
            geos::io::WKBReader wkb_reader (geometry_factory);
            geom_stream.str(PQgetvalue(result, 0, 0));
            linestring = std::unique_ptr<geos::geom::Geometry>(wkb_reader.readHEX(geom_stream));
            PQclear(result);
            if (linestring->getGeometryTypeId() != geos::geom::GeometryTypeId::GEOS_LINESTRING) {
                // if we got no linestring, we will return a unique_ptr which manages nothing (i.e. nullptr equivalent)
                linestring.reset();
            }
            return linestring;
        }
    };

}

#endif /* TABLE_HPP_ */
