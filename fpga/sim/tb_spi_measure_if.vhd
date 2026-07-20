library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity tb_spi_measure_if is
end entity;

architecture sim of tb_spi_measure_if is
    constant CLK_PERIOD : time := 8 ns;

    type byte_array_t is array (natural range <>) of std_logic_vector(7 downto 0);

    signal clk, rst_n                    : std_logic := '0';
    signal spi_cs_n, spi_sck, spi_mosi   : std_logic := '1';
    signal spi_miso                      : std_logic;
    signal freq_word                     : unsigned(47 downto 0);
    signal amplitude_q16                 : unsigned(15 downto 0);
    signal measure_sequence              : unsigned(7 downto 0);
    signal measure_cycles, settle_ms     : unsigned(15 downto 0);
    signal start_measure, clear_result   : std_logic;

    function crc8_update(
        crc_in : std_logic_vector(7 downto 0);
        data_in : std_logic_vector(7 downto 0)
    ) return std_logic_vector is
        variable crc_v : std_logic_vector(7 downto 0) := crc_in;
    begin
        for i in 7 downto 0 loop
            if (crc_v(7) xor data_in(i)) = '1' then
                crc_v := (crc_v(6 downto 0) & '0') xor x"07";
            else
                crc_v := crc_v(6 downto 0) & '0';
            end if;
        end loop;
        return crc_v;
    end function;

    function frame_crc(data : byte_array_t; length : natural)
        return std_logic_vector is
        variable crc_v : std_logic_vector(7 downto 0) := x"00";
    begin
        for i in 0 to length - 1 loop
            crc_v := crc8_update(crc_v, data(i));
        end loop;
        return crc_v;
    end function;

    procedure spi_transfer_byte(
        signal sck  : out std_logic;
        signal mosi : out std_logic;
        signal miso : in  std_logic;
        constant tx : in  std_logic_vector(7 downto 0);
        variable rx : out std_logic_vector(7 downto 0)
    ) is
    begin
        for bit_index in 7 downto 0 loop
            mosi <= tx(bit_index);
            wait for 450 ns;
            sck <= '1';
            wait for 50 ns;
            rx(bit_index) := miso;
            wait for 450 ns;
            sck <= '0';
            wait for 50 ns;
        end loop;
    end procedure;

    procedure spi_transaction(
        signal cs_n : out std_logic;
        signal sck  : out std_logic;
        signal mosi : out std_logic;
        signal miso : in  std_logic;
        constant tx : in  byte_array_t;
        variable rx : out byte_array_t
    ) is
    begin
        cs_n <= '0';
        sck <= '0';
        wait for 1 us;
        for byte_index in tx'range loop
            spi_transfer_byte(sck, mosi, miso, tx(byte_index), rx(byte_index));
        end loop;
        cs_n <= '1';
        sck <= '0';
        mosi <= '0';
        wait for 5 us;
    end procedure;
begin
    clk <= not clk after CLK_PERIOD / 2;

    dut : entity work.spi_measure_if
        port map (
            clk => clk,
            rst_n => rst_n,
            spi_cs_n => spi_cs_n,
            spi_sck => spi_sck,
            spi_mosi => spi_mosi,
            spi_miso => spi_miso,
            measure_busy => '0',
            measure_done => '1',
            result_valid => '1',
            clip_detected => '0',
            overflow_seen => '0',
            i_result => signed'(x"0123456789ABCDEF"),
            q_result => signed'(x"FEDCBA9876543210"),
            sample_count => unsigned'(x"00123456"),
            adc_min => unsigned'(x"12"),
            adc_max => unsigned'(x"E4"),
            freq_word => freq_word,
            amplitude_q16 => amplitude_q16,
            measure_sequence => measure_sequence,
            measure_cycles => measure_cycles,
            settle_ms => settle_ms,
            start_measure => start_measure,
            clear_result => clear_result
        );

    stimulus : process
        variable start_tx, start_rx       : byte_array_t(0 to 7);
        variable request_tx, request_rx   : byte_array_t(0 to 2);
        variable status_tx, status_rx     : byte_array_t(0 to 4);
        variable result_tx, result_rx     : byte_array_t(0 to 32);
    begin
        spi_cs_n <= '1';
        spi_sck <= '0';
        spi_mosi <= '0';
        wait for 10 * CLK_PERIOD;
        rst_n <= '1';
        wait for 1 us;

        start_tx := (x"A5", x"02", x"2A", x"00", x"20", x"00", x"64", x"00");
        start_tx(7) := frame_crc(start_tx, 7);
        spi_transaction(spi_cs_n, spi_sck, spi_mosi, spi_miso,
                        start_tx, start_rx);
        wait for 1 us;

        assert measure_sequence = x"2A" report "START sequence mismatch" severity failure;
        assert measure_cycles = x"0020" report "START cycles mismatch" severity failure;
        assert settle_ms = x"0064" report "START settle time mismatch" severity failure;

        request_tx := (x"A5", x"03", x"00");
        request_tx(2) := frame_crc(request_tx, 2);
        spi_transaction(spi_cs_n, spi_sck, spi_mosi, spi_miso,
                        request_tx, request_rx);
        status_tx := (others => x"00");
        spi_transaction(spi_cs_n, spi_sck, spi_mosi, spi_miso,
                        status_tx, status_rx);

        assert status_rx(0) = x"A5" and status_rx(1) = x"83"
            report "status response header mismatch" severity failure;
        assert status_rx(2) = x"2A" report "status sequence mismatch" severity failure;
        assert status_rx(3) = x"06" report "status flags mismatch" severity failure;
        assert status_rx(4) = frame_crc(status_rx, 4)
            report "status CRC mismatch" severity failure;

        request_tx := (x"A5", x"04", x"00");
        request_tx(2) := frame_crc(request_tx, 2);
        spi_transaction(spi_cs_n, spi_sck, spi_mosi, spi_miso,
                        request_tx, request_rx);
        result_tx := (others => x"00");
        spi_transaction(spi_cs_n, spi_sck, spi_mosi, spi_miso,
                        result_tx, result_rx);

        assert result_rx(0) = x"A5" and result_rx(1) = x"84"
            report "result response header mismatch" severity failure;
        assert result_rx(2) = x"2A" report "result sequence mismatch" severity failure;
        assert result_rx(3 to 8) = byte_array_t'(x"00", x"00", x"00", x"22", x"5C", x"18")
            report "result FTW mismatch" severity failure;
        assert result_rx(9 to 16) = byte_array_t'(
            x"01", x"23", x"45", x"67", x"89", x"AB", x"CD", x"EF")
            report "result I mismatch" severity failure;
        assert result_rx(17 to 24) = byte_array_t'(
            x"FE", x"DC", x"BA", x"98", x"76", x"54", x"32", x"10")
            report "result Q mismatch" severity failure;
        assert result_rx(25 to 28) = byte_array_t'(x"00", x"12", x"34", x"56")
            report "sample count mismatch" severity failure;
        assert result_rx(29) = x"12" and result_rx(30) = x"E4" and result_rx(31) = x"06"
            report "result trailer mismatch" severity failure;
        assert result_rx(32) = frame_crc(result_rx, 32)
            report "result CRC mismatch" severity failure;

        report "tb_spi_measure_if passed" severity note;
        wait;
    end process;
end architecture;
