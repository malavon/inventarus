--
-- File generated with SQLiteStudio.
--
-- Text encoding used: UTF-8
--

-- Table: device
CREATE TABLE IF NOT EXISTS device (
    model         TEXT    PRIMARY KEY ON CONFLICT ROLLBACK,
    datasheet_id  TEXT    REFERENCES datasheet (id) ON DELETE RESTRICT
                          NOT NULL,
    freq_max      INTEGER,
    storage_bytes INTEGER,
    ram_bytes     INTEGER,
    gpio_count    INTEGER,
    uart_count    INTEGER,
    usb_count     INTEGER DEFAULT (0),
    i2c_count     INTEGER,
    spi_count     INTEGER,
    comp_count    INTEGER,
    timer_count   INTEGER,
    op_temp_min   INTEGER,
    op_temp_max   INTEGER,
    comment       TEXT
)
STRICT;

