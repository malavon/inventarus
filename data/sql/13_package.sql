--
-- File generated with SQLiteStudio.
--
-- Text encoding used: UTF-8
--

-- Table: package
CREATE TABLE IF NOT EXISTS package (
    drawing TEXT    NOT NULL,
    pins    INTEGER NOT NULL,
    type    TEXT    NOT NULL,
    comment TEXT,
    CONSTRAINT PK_PACKAGE_DRW_PINS PRIMARY KEY (
        drawing ASC,
        pins ASC
    )
    ON CONFLICT ROLLBACK
)
WITHOUT ROWID,
STRICT;
