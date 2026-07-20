library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;
use IEEE.MATH_REAL.ALL;

entity tb_iq_demod is
end entity;

architecture sim of tb_iq_demod is
    signal clk, rst_n, clear_acc, latch_result, sample_valid, ad_otr : std_logic := '0';
    signal sample_signed, ref_cos, ref_sin : signed(8 downto 0) := (others => '0');
    signal sample_raw, adc_min, adc_max : unsigned(7 downto 0) := (others => '0');
    signal i_result, q_result : signed(63 downto 0);
    signal sample_count : unsigned(31 downto 0);
    signal clip_detected, overflow_seen : std_logic;
begin
    clk <= not clk after 5 ns;

    dut : entity work.iq_demod
        port map (
            clk => clk, rst_n => rst_n, clear_acc => clear_acc,
            latch_result => latch_result, sample_valid => sample_valid,
            sample_signed => sample_signed, sample_raw => sample_raw,
            ref_cos => ref_cos, ref_sin => ref_sin, ad_otr => ad_otr,
            i_result => i_result, q_result => q_result,
            sample_count => sample_count, adc_min => adc_min, adc_max => adc_max,
            clip_detected => clip_detected, overflow_seen => overflow_seen);

    stimulus : process
        variable theta, measured_amplitude, measured_phase : real;
        variable sample_i, cos_i, sin_i : integer;
        variable expected_i, expected_q : integer;
    begin
        wait for 20 ns;
        rst_n <= '1';
        wait until rising_edge(clk);
        clear_acc <= '1';
        wait until rising_edge(clk);
        clear_acc <= '0';

        -- Ten samples: 10*2 on I and 10*(-3) on Q per sample.
        for n in 0 to 9 loop
            sample_signed <= to_signed(10, 9);
            sample_raw <= to_unsigned(120 + n, 8);
            ref_cos <= to_signed(2, 9);
            ref_sin <= to_signed(-3, 9);
            sample_valid <= '1';
            wait until rising_edge(clk);
            sample_valid <= '0';
            for gap in 1 to 4 loop
                wait until rising_edge(clk);
            end loop;
        end loop;

        wait until rising_edge(clk);
        latch_result <= '1';
        wait until rising_edge(clk);
        latch_result <= '0';
        wait for 1 ns;

        assert i_result = to_signed(200, 64) report "I accumulation mismatch" severity failure;
        assert q_result = to_signed(-300, 64) report "Q accumulation mismatch" severity failure;
        assert sample_count = to_unsigned(10, 32) report "sample count mismatch" severity failure;
        assert adc_min = to_unsigned(120, 8) report "ADC minimum mismatch" severity failure;
        assert adc_max = to_unsigned(129, 8) report "ADC maximum mismatch" severity failure;
        assert clip_detected = '0' report "unexpected clipping" severity failure;
        assert overflow_seen = '0' report "unexpected overflow" severity failure;

        -- One complete 256-sample cycle, amplitude 50 codes, phase -30 deg,
        -- and an extra seven-code DC offset.  Integer-cycle I/Q rejects DC.
        clear_acc <= '1';
        wait until rising_edge(clk);
        clear_acc <= '0';
        expected_i := 0;
        expected_q := 0;
        for n in 0 to 255 loop
            theta := 2.0 * MATH_PI * real(n) / 256.0;
            cos_i := integer(round(127.0 * cos(theta)));
            sin_i := integer(round(127.0 * sin(theta)));
            sample_i := integer(round(50.0 * cos(theta + MATH_PI / 6.0) + 7.0));
            expected_i := expected_i + sample_i * cos_i;
            expected_q := expected_q + sample_i * sin_i;
            sample_signed <= to_signed(sample_i, 9);
            sample_raw <= to_unsigned(sample_i + 128, 8);
            ref_cos <= to_signed(cos_i, 9);
            ref_sin <= to_signed(sin_i, 9);
            sample_valid <= '1';
            wait until rising_edge(clk);
            sample_valid <= '0';
            for gap in 1 to 4 loop
                wait until rising_edge(clk);
            end loop;
        end loop;
        wait until rising_edge(clk);
        latch_result <= '1';
        wait until rising_edge(clk);
        latch_result <= '0';
        wait for 1 ns;
        assert i_result = to_signed(expected_i, 64) report "sine-wave I mismatch" severity failure;
        assert q_result = to_signed(expected_q, 64) report "sine-wave Q mismatch" severity failure;
        measured_amplitude := 2.0 * sqrt(real(expected_i) * real(expected_i) +
                                         real(expected_q) * real(expected_q)) /
                              (256.0 * 127.0);
        measured_phase := arctan(real(expected_q), real(expected_i));
        assert abs(measured_amplitude - 50.0) < 0.2
            report "recovered sine amplitude is outside tolerance" severity failure;
        assert abs(measured_phase + MATH_PI / 6.0) < 0.01
            report "recovered sine phase is outside tolerance" severity failure;

        clear_acc <= '1';
        wait until rising_edge(clk);
        clear_acc <= '0';
        sample_signed <= to_signed(127, 9);
        sample_raw <= x"FF";
        ref_cos <= to_signed(1, 9);
        ref_sin <= to_signed(0, 9);
        sample_valid <= '1';
        wait until rising_edge(clk);
        sample_valid <= '0';
        for gap in 1 to 3 loop
            wait until rising_edge(clk);
        end loop;
        latch_result <= '1';
        wait until rising_edge(clk);
        latch_result <= '0';
        wait for 1 ns;
        assert clip_detected = '1' report "full-scale clipping was not detected" severity failure;

        report "tb_iq_demod passed" severity note;
        std.env.stop;
        wait;
    end process;
end architecture;
