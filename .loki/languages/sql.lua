-- SQL language definition
return loki.register_language({
    name = "SQL",
    extensions = {".sql", ".ddl", ".dml"},
    keywords = {
        -- SQL Keywords (Note: SQL is case-insensitive, but we use uppercase for convention)
        "SELECT", "FROM", "WHERE", "INSERT", "UPDATE", "DELETE", "CREATE", "DROP",
        "ALTER", "TABLE", "INDEX", "VIEW", "DATABASE", "SCHEMA", "COLUMN", "PRIMARY",
        "FOREIGN", "KEY", "REFERENCES", "CONSTRAINT", "UNIQUE", "NOT", "NULL", "DEFAULT",
        "AUTO_INCREMENT", "IDENTITY", "SERIAL",
        "AND", "OR", "IN", "BETWEEN", "LIKE", "IS", "EXISTS",
        "ANY", "ALL", "SOME", "UNION", "INTERSECT", "EXCEPT", "INNER", "LEFT", "RIGHT",
        "FULL", "OUTER", "JOIN", "ON", "USING", "GROUP", "BY", "HAVING", "ORDER", "ASC",
        "DESC", "LIMIT", "OFFSET", "DISTINCT", "AS", "CASE", "WHEN", "THEN", "ELSE", "END",
        "IF", "IFNULL", "ISNULL", "COALESCE", "NULLIF", "CAST", "CONVERT", "SUBSTRING",
        "LENGTH", "UPPER", "LOWER", "TRIM", "LTRIM", "RTRIM", "REPLACE", "CONCAT",
        "CURRENT_DATE", "CURRENT_TIME", "CURRENT_TIMESTAMP", "NOW", "COUNT", "SUM",
        "AVG", "MIN", "MAX", "STDDEV", "VARIANCE", "BEGIN", "COMMIT", "ROLLBACK",
        "TRANSACTION", "SAVEPOINT", "GRANT", "REVOKE", "LOCK", "UNLOCK",
    },
    types = {
        -- SQL Data Types
        "BOOLEAN", "TINYINT", "SMALLINT", "MEDIUMINT", "INT", "INTEGER", "BIGINT",
        "DECIMAL", "NUMERIC", "FLOAT", "DOUBLE", "REAL", "BIT",
        "DATE", "TIME", "DATETIME", "TIMESTAMP", "YEAR",
        "CHAR", "VARCHAR", "BINARY", "VARBINARY",
        "TINYBLOB", "BLOB", "MEDIUMBLOB", "LONGBLOB",
        "TINYTEXT", "TEXT", "MEDIUMTEXT", "LONGTEXT",
        "ENUM", "SET", "JSON", "GEOMETRY", "POINT",
        "LINESTRING", "POLYGON", "MULTIPOINT", "MULTILINESTRING", "MULTIPOLYGON",
        "GEOMETRYCOLLECTION",
        -- SQL Constants
        "TRUE", "FALSE", "UNKNOWN",
    },
    line_comment = "--",
    block_comment_start = "/*",
    block_comment_end = "*/",
    separators = ",.()+-/*=~%<>[];",
    highlight_strings = true,
    highlight_numbers = true
})
