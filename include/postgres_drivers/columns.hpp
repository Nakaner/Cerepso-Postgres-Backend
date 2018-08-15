/*
 * columns.hpp
 *
 *  Created on: 24.06.2016
 *      Author: michael
 */

#ifndef COLUMNS_HPP_
#define COLUMNS_HPP_

#include <string>
#include <vector>

#include <osmium/osm/metadata_options.hpp>

namespace postgres_drivers {

    /**
     * \brief Typed enum which defines the geometry type of the table.
     *
     * The types are different from simple features because OSM does not
     * follow OGC Simple Feature Specification. For example, nod
     */
    enum class TableType : char {
        /// nodes with tags
        POINT = 1,
        /// nodes without tags
        UNTAGGED_POINT = 2,
        /// ways
        WAYS_LINEAR = 3,
        /// ways which are polygons
        WAYS_POLYGON = 4,
        /// relations which are multipolygons
        RELATION_POLYGON = 5,
        /// relations
        RELATION_OTHER = 6
    };

    /**
     * program configuration
     *
     * \todo remove members which are only neccesary for write access
     */
    struct Config {
        /// debug mode enabled
        bool m_debug = false;
        /// name of the database
        std::string m_database_name = "pgimportertest";
        /// store tags as hstore \unsupported
        bool tags_hstore = true;

        /**
         * Import metadata of OSM objects into the database.
         * This increase the size of the database a lot.
         */
        osmium::metadata_options metadata = osmium::metadata_options{"none"};
    };

    typedef std::pair<const std::string, const std::string> Column;
    typedef std::vector<Column> ColumnsVector;

    using ColumnsIterator = ColumnsVector::iterator;

    /**
     * \brief This class holds the names and types of the columns of a database table.
     *
     * This class implements the iterator pattern. The provided iterator works like an STL iterator.
     *
     * \todo Add method which reads additional columns config from file and returns an instance of Columns.
     */
    class Columns {
    private:
        ColumnsVector m_columns;
        TableType m_type;

    public:
        Columns() = delete;

        Columns(Config& config, TableType type):
                m_columns(),
                m_type(type) {
            m_columns.push_back(std::make_pair("osm_id", "bigint"));
            if (config.tags_hstore && type != TableType::UNTAGGED_POINT) {
                m_columns.push_back(std::make_pair("tags", "hstore"));
            }
            if (config.metadata.user()) {
                m_columns.push_back(std::make_pair("osm_user", "text"));
            }
            if (config.metadata.uid()) {
                m_columns.push_back(std::make_pair("osm_uid", "bigint"));
            }
            if (config.metadata.version()) {
                m_columns.push_back(std::make_pair("osm_version", "integer"));
            }
            if (config.metadata.timestamp()) {
                m_columns.push_back(std::make_pair("osm_lastmodified", "char(23)"));
            }
            if (config.metadata.changeset()) {
                m_columns.push_back(std::make_pair("osm_changeset", "bigint"));
            }
            switch (type) {
            case TableType::POINT :
                m_columns.push_back(std::make_pair("geom", "geometry(Point,4326)"));
                break;
            case TableType::UNTAGGED_POINT :
                m_columns.push_back(std::make_pair("geom", "geometry(Point,4326)"));
                break;
            case TableType::WAYS_LINEAR :
                m_columns.push_back(std::make_pair("geom", "geometry(LineString,4326)"));
                m_columns.push_back(std::make_pair("way_nodes", "bigint[]"));
                break;
            case TableType::WAYS_POLYGON :
                m_columns.push_back(std::make_pair("geom", "geometry(MultiPolygon,4326)"));
                m_columns.push_back(std::make_pair("way_nodes", "bigint[]"));
                break;
            case TableType::RELATION_POLYGON :
                m_columns.push_back(std::make_pair("geom", "geometry(MultiPolygon,4326)"));
        //        m_columns.push_back(std::make_pair("member_ids", "bigint[]"));
        //        m_columns.push_back(std::make_pair("member_types", "char[]"));
                break;
            case TableType::RELATION_OTHER :
                m_columns.push_back(std::make_pair("geom", "geometry(GeometryCollection,4326)"));
                m_columns.push_back(std::make_pair("member_ids", "bigint[]"));
                m_columns.push_back(std::make_pair("member_types", "char[]"));
            }
        }

        Columns(Config& config, ColumnsVector& additional_columns, TableType type) :
            Columns::Columns(config, type) {
            for (Column& col : additional_columns) {
                m_columns.push_back(col);
            }
        }

        //TODO add method which reads additional columns config from file and returns an instance of Columns

        ColumnsIterator begin() {
            return m_columns.begin();
        }

        ColumnsIterator end() {
            return m_columns.end();
        }

        Column& front() {
            return m_columns.front();
        }

        Column& back() {
            return m_columns.back();
        }

        Column& at(size_t n) {
            return m_columns.at(n);
        }

        /**
         * \brief Get number of columns of this table.
         */
        int size() {
            return m_columns.size();
        }

        /**
         * \brief Get name of n-th (0 is first) column.
         *
         * \param n column index
         *
         * \returns column name
         */
        const std::string& column_name_at(size_t n) {
            return m_columns.at(n).first;
        }

        /**
         * \brief Get type of n-th (0 is first) column.
         *
         * \param n column index
         *
         * \returns column type
         */
        const std::string& column_type_at(size_t n) {
            return m_columns.at(n).second;
        }

        /**
         * \brief Get geometry type of this table.
         */
        TableType get_type() {
            return m_type;
        }
    };
}


#endif /* COLUMNS_HPP_ */
