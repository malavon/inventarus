--
-- File generated with SQLiteStudio.
--
-- Text encoding used: UTF-8
--

-- Table: datasheet
CREATE TABLE IF NOT EXISTS datasheet (
    id          TEXT    PRIMARY KEY ON CONFLICT ROLLBACK,
    revision    TEXT,
    issue_month INTEGER NOT NULL,
    issue_year  INTEGER NOT NULL,
    rev_month   INTEGER NOT NULL,
    rev_year    INTEGER NOT NULL,
    comment     TEXT
)
WITHOUT ROWID,
STRICT;

