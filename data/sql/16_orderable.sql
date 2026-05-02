--
-- File generated with SQLiteStudio.
--
-- Text encoding used: UTF-8
--

-- Table: orderable
CREATE TABLE IF NOT EXISTS orderable (
    name        TEXT    UNIQUE ON CONFLICT ROLLBACK
                        NOT NULL
                        PRIMARY KEY,
    device_id   TEXT    REFERENCES device (model) ON DELETE RESTRICT
                                                  ON UPDATE CASCADE
                        NOT NULL,
    drawing     TEXT    NOT NULL,
    pins        INTEGER NOT NULL,
    pinset_id   INTEGER REFERENCES pinset (id) ON DELETE RESTRICT
                                               ON UPDATE CASCADE,
    status      TEXT    NOT NULL,
    msl_level   INTEGER,
    op_temp_min INTEGER,
    op_temp_max INTEGER,
    comment     TEXT,
    CONSTRAINT FK_ODBL_PACKAGE FOREIGN KEY (
        drawing,
        pins
    )
    REFERENCES package (drawing,
    pins) MATCH [FULL]
)
WITHOUT ROWID,
STRICT;
