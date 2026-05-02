--
-- File generated with SQLiteStudio.
--
-- Text encoding used: UTF-8
--

-- Table: feature
CREATE TABLE IF NOT EXISTS feature (
    id                 TEXT PRIMARY KEY,
    param1_description TEXT,
    param2_description ANY,
    param3_description ANY,
    comment            TEXT,
    family_text        TEXT,
    family_group       TEXT,
    signal_group       TEXT
)
WITHOUT ROWID,
STRICT;

-- Table: device_feature
CREATE TABLE IF NOT EXISTS device_feature (
    device_id  TEXT NOT NULL
                    REFERENCES device (model) ON DELETE CASCADE
                                              ON UPDATE CASCADE,
    feature_id TEXT REFERENCES feature (id) ON DELETE RESTRICT
                    NOT NULL,
    param1     ANY,
    param2     ANY,
    param3     ANY,
    comment    TEXT
)
STRICT;
