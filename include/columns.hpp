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

namespace postgres_drivers {

    enum class TableType : char {
        POINT = 1,
        UNTAGGED_POINT = 2,
        WAYS_LINEAR = 3,
        WAYS_POLYGON = 4,
        RELATION_POLYGON = 5,
        RELATION_OTHER = 6
    };

    /**
     * program configuration
     *
     * \todo remove members which are only neccesary for write access
     */
    struct Config {
        bool m_debug = false;
        std::string m_database_name = "pgimportertest";
        bool tags_hstore = true;
        bool metadata = true;
        bool m_all_geom_indexes = false;
        bool m_geom_indexes = true;
        bool m_order_by_geohash = true;
        bool m_append = false;
        bool m_id_index = true;
        bool m_usernames = true;
    };

    typedef std::pair<const std::string, const std::string> Column;
    typedef std::vector<Column> ColumnsVector;

    using ColumnsIterator = ColumnsVector::iterator;

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
            if (config.metadata) {
                if (config.m_usernames) {
                    m_columns.push_back(std::make_pair("osm_user", "text"));
                }
                m_columns.push_back(std::make_pair("osm_uid", "bigint"));
                m_columns.push_back(std::make_pair("osm_version", "integer"));
                m_columns.push_back(std::make_pair("osm_lastmodified", "char(23)"));
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

        int size() {
            return m_columns.size();
        }

        /**
         * returns name of n-th (0 is first) column
         */
        const std::string& column_name_at(size_t n) {
            return m_columns.at(n).first;
        }

        /**
         * returns type of n-th (0 is first) column
         */
        const std::string& column_type_at(size_t n) {
            return m_columns.at(n).second;
        }

        TableType get_type() {
            return m_type;
        }
    };
}


#endif /* COLUMNS_HPP_ */
