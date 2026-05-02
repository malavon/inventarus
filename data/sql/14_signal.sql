--
-- File generated with SQLiteStudio.
--
-- Text encoding used: UTF-8
--

-- Table: signalgroup
CREATE TABLE IF NOT EXISTS signalgroup (
    name TEXT PRIMARY KEY ON CONFLICT FAIL
)
WITHOUT ROWID;

-- SQLite doesn't understand ALTER TABLE x ALTER COLUMN ...

ALTER TABLE feature DROP COLUMN signal_group;
ALTER TABLE feature ADD signal_group TEXT REFERENCES signalgroup (name) ON DELETE RESTRICT
                                                                        ON UPDATE CASCADE;

-- Table: signal
CREATE TABLE IF NOT EXISTS signal (
    id          TEXT PRIMARY KEY ON CONFLICT ROLLBACK,
    alias_for   TEXT REFERENCES signal (id) ON DELETE CASCADE
                                            ON UPDATE CASCADE,
    desc        TEXT NOT NULL,
    signalgroup TEXT REFERENCES signalgroup (name) 
)
WITHOUT ROWID,
STRICT;

-- Table: signalset
CREATE TABLE IF NOT EXISTS signalset (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    parent_id     INTEGER REFERENCES signalset (id) ON DELETE RESTRICT
                                                    ON UPDATE CASCADE,
    datasheet_idx INTEGER NOT NULL,
    CONSTRAINT [UQ_SIGNALSET_ID+IDX] UNIQUE (
        id,
        datasheet_idx
    )
);

-- Table: signalset_signal
CREATE TABLE IF NOT EXISTS signalset_signal (
    signalset_id INTEGER REFERENCES signalset (id) ON DELETE RESTRICT
                                                   ON UPDATE CASCADE
                         NOT NULL,
    signal_id    TEXT    REFERENCES signal (id) ON DELETE RESTRICT
                                                ON UPDATE CASCADE
                         NOT NULL,
    idx          INTEGER NOT NULL,
    PRIMARY KEY (
        signalset_id,
        signal_id
    )
);
