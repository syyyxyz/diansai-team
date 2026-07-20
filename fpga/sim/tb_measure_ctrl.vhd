library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity tb_measure_ctrl is
end entity;

architecture sim of tb_measure_ctrl is
    signal clk, rst_n, start, clear_result, phase_wrap : std_logic := '0';
    signal requested_cycles : unsigned(15 downto 0) := to_unsigned(3, 16);
    signal settle_ms : unsigned(15 downto 0) := to_unsigned(2, 16);
    signal capture_en, clear_acc, latch_result, busy, done, result_valid : std_logic;
begin
    clk <= not clk after 5 ns;

    dut : entity work.measure_ctrl
        generic map (CLOCK_HZ => 2000)
        port map (
            clk => clk, rst_n => rst_n, start => start,
            clear_result => clear_result, phase_wrap => phase_wrap,
            requested_cycles => requested_cycles, settle_ms => settle_ms,
            capture_en => capture_en, clear_acc => clear_acc,
            latch_result => latch_result, busy => busy, done => done,
            result_valid => result_valid);

    stimulus : process
        procedure pulse_wrap is
        begin
            phase_wrap <= '1';
            wait until rising_edge(clk);
            phase_wrap <= '0';
            wait until rising_edge(clk);
        end procedure;
    begin
        wait for 20 ns;
        rst_n <= '1';
        wait until rising_edge(clk);
        start <= '1';
        wait until rising_edge(clk);
        start <= '0';
        wait for 1 ns;
        assert busy = '1' report "controller did not enter busy state" severity failure;

        -- A wrap during the settling interval must not start capture.
        pulse_wrap;
        assert capture_en = '0' report "capture started before settling completed" severity failure;

        -- Wait for WAIT_WRAP, then align the measurement window.
        for n in 1 to 8 loop
            wait until rising_edge(clk);
        end loop;
        pulse_wrap;
        wait until rising_edge(clk);
        assert capture_en = '1' report "capture did not start on an aligned wrap" severity failure;

        pulse_wrap;
        assert done = '0' report "measurement ended after one cycle" severity failure;
        pulse_wrap;
        assert done = '0' report "measurement ended after two cycles" severity failure;
        pulse_wrap;

        for n in 1 to 8 loop
            wait until rising_edge(clk);
            exit when done = '1';
        end loop;
        assert done = '1' report "measurement did not finish after three cycles" severity failure;
        assert result_valid = '1' report "result was not published" severity failure;
        assert busy = '0' report "busy remained set in DONE" severity failure;

        clear_result <= '1';
        wait until rising_edge(clk);
        clear_result <= '0';
        wait until rising_edge(clk);
        assert done = '0' and result_valid = '0' report "clear result failed" severity failure;

        report "tb_measure_ctrl passed" severity note;
        std.env.stop;
        wait;
    end process;
end architecture;
