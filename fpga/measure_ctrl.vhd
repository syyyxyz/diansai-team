library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity measure_ctrl is
    generic (
        CLOCK_HZ : positive := 125000000
    );
    port (
        clk              : in  std_logic;
        rst_n            : in  std_logic;
        start            : in  std_logic;
        clear_result     : in  std_logic;
        phase_wrap       : in  std_logic;
        requested_cycles : in  unsigned(15 downto 0);
        settle_ms        : in  unsigned(15 downto 0);
        capture_en       : out std_logic;
        clear_acc        : out std_logic;
        latch_result     : out std_logic;
        busy             : out std_logic;
        done             : out std_logic;
        result_valid     : out std_logic
    );
end entity;

architecture rtl of measure_ctrl is
    type state_t is (IDLE, SETTLE, WAIT_WRAP, CLEAR, MEASURE, DRAIN, LATCH, DONE_STATE);
    signal state         : state_t := IDLE;
    signal ms_divider    : integer range 0 to CLOCK_HZ / 1000 - 1 := 0;
    signal ms_remaining  : unsigned(15 downto 0) := (others => '0');
    signal cycle_target  : unsigned(15 downto 0) := to_unsigned(100, 16);
    signal cycles_left   : unsigned(15 downto 0) := to_unsigned(100, 16);
    signal drain_count   : integer range 0 to 3 := 0;
    signal valid_reg     : std_logic := '0';
begin
    capture_en   <= '1' when state = MEASURE else '0';
    clear_acc    <= '1' when state = CLEAR else '0';
    busy         <= '1' when state /= IDLE and state /= DONE_STATE else '0';
    done         <= '1' when state = DONE_STATE else '0';
    result_valid <= valid_reg;

    process(clk, rst_n)
    begin
        if rst_n = '0' then
            state          <= IDLE;
            ms_divider     <= 0;
            ms_remaining   <= (others => '0');
            cycle_target   <= to_unsigned(100, 16);
            cycles_left    <= to_unsigned(100, 16);
            drain_count    <= 0;
            latch_result   <= '0';
            valid_reg      <= '0';
        elsif rising_edge(clk) then
            latch_result <= '0';

            if clear_result = '1' then
                valid_reg <= '0';
                if state = DONE_STATE then
                    state <= IDLE;
                end if;
            end if;

            case state is
                when IDLE | DONE_STATE =>
                    if start = '1' then
                        valid_reg    <= '0';
                        ms_divider   <= 0;
                        ms_remaining <= settle_ms;
                        if requested_cycles = to_unsigned(0, 16) then
                            cycle_target <= to_unsigned(100, 16);
                        else
                            cycle_target <= requested_cycles;
                        end if;
                        if settle_ms = to_unsigned(0, 16) then
                            state <= WAIT_WRAP;
                        else
                            state <= SETTLE;
                        end if;
                    end if;

                when SETTLE =>
                    if ms_divider = CLOCK_HZ / 1000 - 1 then
                        ms_divider <= 0;
                        if ms_remaining <= to_unsigned(1, 16) then
                            ms_remaining <= (others => '0');
                            state <= WAIT_WRAP;
                        else
                            ms_remaining <= ms_remaining - 1;
                        end if;
                    else
                        ms_divider <= ms_divider + 1;
                    end if;

                when WAIT_WRAP =>
                    if phase_wrap = '1' then
                        state <= CLEAR;
                    end if;

                when CLEAR =>
                    cycles_left <= cycle_target;
                    state <= MEASURE;

                when MEASURE =>
                    if phase_wrap = '1' then
                        if cycles_left <= to_unsigned(1, 16) then
                            drain_count <= 0;
                            state <= DRAIN;
                        else
                            cycles_left <= cycles_left - 1;
                        end if;
                    end if;

                when DRAIN =>
                    if drain_count = 2 then
                        latch_result <= '1';
                        state <= LATCH;
                    else
                        drain_count <= drain_count + 1;
                    end if;

                when LATCH =>
                    -- The IQ block observes latch_result on this edge.  Publish
                    -- valid one clock later so software cannot see stale data.
                    valid_reg <= '1';
                    state <= DONE_STATE;
            end case;
        end if;
    end process;
end rtl;
