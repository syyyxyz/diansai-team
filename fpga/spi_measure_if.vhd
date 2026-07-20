library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity spi_measure_if is
    generic (
        PHASE_WIDTH : positive := 48
    );
    port (
        clk              : in  std_logic;
        rst_n            : in  std_logic;
        spi_cs_n         : in  std_logic;
        spi_sck          : in  std_logic;
        spi_mosi         : in  std_logic;
        spi_miso         : out std_logic;

        measure_busy     : in  std_logic;
        measure_done     : in  std_logic;
        result_valid     : in  std_logic;
        clip_detected    : in  std_logic;
        overflow_seen    : in  std_logic;
        i_result         : in  signed(63 downto 0);
        q_result         : in  signed(63 downto 0);
        sample_count     : in  unsigned(31 downto 0);
        adc_min          : in  unsigned(7 downto 0);
        adc_max          : in  unsigned(7 downto 0);

        freq_word        : out unsigned(PHASE_WIDTH - 1 downto 0);
        amplitude_q16    : out unsigned(15 downto 0);
        measure_sequence : out unsigned(7 downto 0);
        measure_cycles   : out unsigned(15 downto 0);
        settle_ms        : out unsigned(15 downto 0);
        start_measure    : out std_logic;
        clear_result     : out std_logic
    );
end entity;

architecture rtl of spi_measure_if is
    constant FRAME_SOF        : std_logic_vector(7 downto 0) := x"A5";
    constant CMD_SET_SINE     : std_logic_vector(7 downto 0) := x"01";
    constant CMD_START        : std_logic_vector(7 downto 0) := x"02";
    constant CMD_READ_STATUS  : std_logic_vector(7 downto 0) := x"03";
    constant CMD_READ_RESULT  : std_logic_vector(7 downto 0) := x"04";
    constant CMD_CLEAR_RESULT : std_logic_vector(7 downto 0) := x"05";

    type byte_array_t is array (0 to 32) of std_logic_vector(7 downto 0);

    function crc8_update(
        crc_in  : std_logic_vector(7 downto 0);
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

    signal cs_meta, cs_sync, cs_prev       : std_logic := '1';
    signal sck_meta, sck_sync, sck_prev    : std_logic := '0';
    signal mosi_meta, mosi_sync            : std_logic := '0';
    signal rx_shift                        : std_logic_vector(7 downto 0) := (others => '0');
    signal rx_crc                          : std_logic_vector(7 downto 0) := (others => '0');
    signal rx_command                      : std_logic_vector(7 downto 0) := (others => '0');
    signal rx_bit_index                    : integer range 0 to 7 := 0;
    signal rx_byte_index                   : integer range 0 to 10 := 0;
    signal rx_freq                         : unsigned(PHASE_WIDTH - 1 downto 0) := (others => '0');
    signal rx_amplitude                    : unsigned(15 downto 0) := (others => '0');
    signal rx_sequence                     : unsigned(7 downto 0) := (others => '0');
    signal rx_cycles                       : unsigned(15 downto 0) := to_unsigned(100, 16);
    signal rx_settle                       : unsigned(15 downto 0) := to_unsigned(500, 16);
    signal response_bytes                  : byte_array_t := (others => (others => '0'));
    signal tx_byte_index                   : integer range 0 to 32 := 0;
    signal tx_bit_index                    : integer range 0 to 7 := 0;
    signal protocol_error                  : std_logic := '0';
    signal freq_word_reg                   : unsigned(PHASE_WIDTH - 1 downto 0) := (others => '0');
    signal sequence_reg                    : unsigned(7 downto 0) := (others => '0');
    signal prepare_status                  : std_logic := '0';
    signal prepare_result                  : std_logic := '0';
    signal response_building               : std_logic := '0';
    signal response_payload_length         : integer range 0 to 32 := 0;
    signal response_build_index            : integer range 0 to 32 := 0;
    signal response_crc                    : std_logic_vector(7 downto 0) := (others => '0');
begin
    freq_word        <= freq_word_reg;
    measure_sequence <= sequence_reg;
    spi_miso <= response_bytes(tx_byte_index)(7 - tx_bit_index) when cs_sync = '0' else '0';

    process(clk, rst_n)
    begin
        if rst_n = '0' then
            cs_meta   <= '1';
            cs_sync   <= '1';
            cs_prev   <= '1';
            sck_meta  <= '0';
            sck_sync  <= '0';
            sck_prev  <= '0';
            mosi_meta <= '0';
            mosi_sync <= '0';
        elsif rising_edge(clk) then
            cs_meta   <= spi_cs_n;
            cs_sync   <= cs_meta;
            cs_prev   <= cs_sync;
            sck_meta  <= spi_sck;
            sck_sync  <= sck_meta;
            sck_prev  <= sck_sync;
            mosi_meta <= spi_mosi;
            mosi_sync <= mosi_meta;
        end if;
    end process;

    -- Build response CRC one byte per core clock.  Unrolling a 32-byte CRC in
    -- one assignment produced a very long combinational path on Cyclone IV.
    process(clk, rst_n)
        variable status_byte : std_logic_vector(7 downto 0);
    begin
        if rst_n = '0' then
            response_bytes          <= (others => (others => '0'));
            response_building       <= '0';
            response_payload_length <= 0;
            response_build_index    <= 0;
            response_crc            <= (others => '0');
        elsif rising_edge(clk) then
            if prepare_status = '1' then
                status_byte := "00" & protocol_error & overflow_seen &
                               clip_detected & result_valid & measure_done & measure_busy;
                response_bytes(0) <= FRAME_SOF;
                response_bytes(1) <= x"83";
                response_bytes(2) <= std_logic_vector(sequence_reg);
                response_bytes(3) <= status_byte;
                response_payload_length <= 4;
                response_build_index <= 0;
                response_crc <= x"00";
                response_building <= '1';
            elsif prepare_result = '1' then
                status_byte := "00" & protocol_error & overflow_seen &
                               clip_detected & result_valid & measure_done & measure_busy;
                response_bytes(0) <= FRAME_SOF;
                response_bytes(1) <= x"84";
                response_bytes(2) <= std_logic_vector(sequence_reg);
                for i in 0 to 5 loop
                    response_bytes(3 + i) <= std_logic_vector(
                        freq_word_reg(PHASE_WIDTH - 1 - i*8 downto PHASE_WIDTH - 8 - i*8));
                end loop;
                for i in 0 to 7 loop
                    response_bytes(9 + i) <= std_logic_vector(i_result(63 - i*8 downto 56 - i*8));
                    response_bytes(17 + i) <= std_logic_vector(q_result(63 - i*8 downto 56 - i*8));
                end loop;
                for i in 0 to 3 loop
                    response_bytes(25 + i) <= std_logic_vector(
                        sample_count(31 - i*8 downto 24 - i*8));
                end loop;
                response_bytes(29) <= std_logic_vector(adc_min);
                response_bytes(30) <= std_logic_vector(adc_max);
                response_bytes(31) <= status_byte;
                response_payload_length <= 32;
                response_build_index <= 0;
                response_crc <= x"00";
                response_building <= '1';
            elsif response_building = '1' then
                if response_build_index = response_payload_length then
                    response_bytes(response_payload_length) <= response_crc;
                    response_building <= '0';
                else
                    response_crc <= crc8_update(
                        response_crc, response_bytes(response_build_index));
                    response_build_index <= response_build_index + 1;
                end if;
            end if;
        end if;
    end process;

    process(clk, rst_n)
    begin
        if rst_n = '0' then
            tx_byte_index <= 0;
            tx_bit_index  <= 0;
        elsif rising_edge(clk) then
            if cs_prev = '1' and cs_sync = '0' then
                tx_byte_index <= 0;
                tx_bit_index  <= 0;
            elsif cs_sync = '0' and sck_prev = '1' and sck_sync = '0' then
                if tx_bit_index = 7 then
                    tx_bit_index <= 0;
                    if tx_byte_index < 32 then
                        tx_byte_index <= tx_byte_index + 1;
                    end if;
                else
                    tx_bit_index <= tx_bit_index + 1;
                end if;
            end if;
        end if;
    end process;

    process(clk, rst_n)
        variable received_byte : std_logic_vector(7 downto 0);
    begin
        if rst_n = '0' then
            rx_shift         <= (others => '0');
            rx_crc           <= (others => '0');
            rx_command       <= (others => '0');
            rx_bit_index     <= 0;
            rx_byte_index    <= 0;
            rx_freq          <= (others => '0');
            rx_amplitude     <= (others => '0');
            rx_sequence      <= (others => '0');
            rx_cycles        <= to_unsigned(100, 16);
            rx_settle        <= to_unsigned(500, 16);
            freq_word_reg    <= to_unsigned(2251800, PHASE_WIDTH);
            amplitude_q16    <= (others => '0');
            sequence_reg     <= (others => '0');
            measure_cycles   <= to_unsigned(100, 16);
            settle_ms        <= to_unsigned(500, 16);
            start_measure    <= '0';
            clear_result     <= '0';
            protocol_error   <= '0';
            prepare_status   <= '0';
            prepare_result   <= '0';
        elsif rising_edge(clk) then
            start_measure <= '0';
            clear_result  <= '0';
            prepare_status <= '0';
            prepare_result <= '0';

            if cs_sync = '1' then
                rx_shift      <= (others => '0');
                rx_crc        <= (others => '0');
                rx_bit_index  <= 0;
                rx_byte_index <= 0;
            elsif sck_prev = '0' and sck_sync = '1' then
                received_byte := rx_shift(6 downto 0) & mosi_sync;
                rx_shift <= received_byte;

                if rx_bit_index = 7 then
                    rx_bit_index <= 0;
                    case rx_byte_index is
                        when 0 =>
                            if received_byte = FRAME_SOF then
                                rx_crc <= crc8_update(x"00", received_byte);
                                rx_byte_index <= 1;
                            else
                                rx_byte_index <= 0;
                            end if;

                        when 1 =>
                            rx_command <= received_byte;
                            if received_byte = CMD_SET_SINE or received_byte = CMD_START or
                               received_byte = CMD_READ_STATUS or received_byte = CMD_READ_RESULT or
                               received_byte = CMD_CLEAR_RESULT then
                                rx_crc <= crc8_update(rx_crc, received_byte);
                                rx_byte_index <= 2;
                            else
                                protocol_error <= '1';
                                rx_byte_index <= 0;
                            end if;

                        when 2 =>
                            if rx_command = CMD_READ_STATUS or rx_command = CMD_READ_RESULT or
                               rx_command = CMD_CLEAR_RESULT then
                                if received_byte = rx_crc then
                                    protocol_error <= '0';
                                    if rx_command = CMD_READ_STATUS then
                                        prepare_status <= '1';
                                    elsif rx_command = CMD_READ_RESULT then
                                        prepare_result <= '1';
                                    else
                                        clear_result <= '1';
                                    end if;
                                else
                                    protocol_error <= '1';
                                end if;
                                rx_byte_index <= 0;
                            elsif rx_command = CMD_SET_SINE then
                                rx_freq(PHASE_WIDTH - 1 downto PHASE_WIDTH - 8) <= unsigned(received_byte);
                                rx_crc <= crc8_update(rx_crc, received_byte);
                                rx_byte_index <= 3;
                            else
                                rx_sequence <= unsigned(received_byte);
                                rx_crc <= crc8_update(rx_crc, received_byte);
                                rx_byte_index <= 3;
                            end if;

                        when 3 to 7 =>
                            if rx_command = CMD_SET_SINE then
                                rx_freq(PHASE_WIDTH - 1 - (rx_byte_index-2)*8 downto PHASE_WIDTH - 8 - (rx_byte_index-2)*8) <= unsigned(received_byte);
                                rx_crc <= crc8_update(rx_crc, received_byte);
                                rx_byte_index <= rx_byte_index + 1;
                            elsif rx_command = CMD_START then
                                case rx_byte_index is
                                    when 3 => rx_cycles(15 downto 8) <= unsigned(received_byte);
                                    when 4 => rx_cycles(7 downto 0) <= unsigned(received_byte);
                                    when 5 => rx_settle(15 downto 8) <= unsigned(received_byte);
                                    when 6 => rx_settle(7 downto 0) <= unsigned(received_byte);
                                    when others => null;
                                end case;
                                if rx_byte_index = 7 then
                                    if received_byte = rx_crc then
                                        if measure_busy = '0' then
                                            sequence_reg <= rx_sequence;
                                            measure_cycles <= rx_cycles;
                                            settle_ms <= rx_settle;
                                            start_measure <= '1';
                                            protocol_error <= '0';
                                        else
                                            protocol_error <= '1';
                                        end if;
                                    else
                                        protocol_error <= '1';
                                    end if;
                                    rx_byte_index <= 0;
                                else
                                    rx_crc <= crc8_update(rx_crc, received_byte);
                                    rx_byte_index <= rx_byte_index + 1;
                                end if;
                            else
                                rx_byte_index <= 0;
                            end if;

                        when 8 =>
                            rx_amplitude(15 downto 8) <= unsigned(received_byte);
                            rx_crc <= crc8_update(rx_crc, received_byte);
                            rx_byte_index <= 9;
                        when 9 =>
                            rx_amplitude(7 downto 0) <= unsigned(received_byte);
                            rx_crc <= crc8_update(rx_crc, received_byte);
                            rx_byte_index <= 10;
                        when others =>
                            if received_byte = rx_crc and measure_busy = '0' then
                                freq_word_reg <= rx_freq;
                                amplitude_q16 <= rx_amplitude;
                                protocol_error <= '0';
                            else
                                protocol_error <= '1';
                            end if;
                            rx_byte_index <= 0;
                    end case;
                else
                    rx_bit_index <= rx_bit_index + 1;
                end if;
            end if;
        end if;
    end process;
end rtl;
