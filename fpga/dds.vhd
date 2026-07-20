library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity dds is
    port (
        clk        : in  std_logic;
        rst_n      : in  std_logic;
        spi_cs_n   : in  std_logic;
        spi_sck    : in  std_logic;
        spi_mosi   : in  std_logic;
        spi_miso   : out std_logic;
        da_clk     : out std_logic;
        da_data    : out std_logic_vector(7 downto 0);
        ad_clk_out : out std_logic;
        ad_data    : in  std_logic_vector(7 downto 0)
    );
end dds;

architecture rtl of dds is
    constant PHASE_WIDTH : positive := 48;
    subtype phase_word_t is unsigned(PHASE_WIDTH - 1 downto 0);
    constant FREQ_MIN : phase_word_t := x"000000225C18";
    constant FREQ_MAX : phase_word_t := x"7AE147AE147B";

    component pll_125m is
        port (areset : in std_logic; inclk0 : in std_logic;
              c0 : out std_logic; locked : out std_logic);
    end component;
    component altddio_out is
        generic (intended_device_family : string := "unused"; width : natural);
        port (
            aclr : in std_logic := '0'; aset : in std_logic := '0';
            datain_h : in std_logic_vector(width - 1 downto 0);
            datain_l : in std_logic_vector(width - 1 downto 0);
            dataout : out std_logic_vector(width - 1 downto 0);
            oe : in std_logic := '1'; oe_out : out std_logic_vector(width - 1 downto 0);
            outclock : in std_logic; outclocken : in std_logic := '1';
            sclr : in std_logic := '0'; sset : in std_logic := '0');
    end component;
    component sine_rom is
        port (address : in std_logic_vector(7 downto 0);
              clock : in std_logic := '1'; q : out std_logic_vector(7 downto 0));
    end component;

    signal clk_core, pll_locked, core_rst_n : std_logic;
    signal reset_pipe : std_logic_vector(1 downto 0) := (others => '0');
    signal da_clk_ddio : std_logic_vector(0 downto 0);

    signal phase_acc : phase_word_t := (others => '0');
    signal phase_wrap : std_logic := '0';
    signal freq_word_cmd, freq_word : phase_word_t := FREQ_MIN;
    signal amplitude_q16 : unsigned(15 downto 0) := (others => '0');
    signal rom_addr, rom_data : std_logic_vector(7 downto 0);
    signal da_data_reg : std_logic_vector(7 downto 0) := x"80";
    signal centered_sample_pipe : signed(8 downto 0) := (others => '0');
    signal gain_pipe : signed(16 downto 0) := (others => '0');
    signal scaled_product_pipe : signed(25 downto 0) := (others => '0');
    signal bypass_sample_pipe_1, bypass_sample_pipe_2 : std_logic_vector(7 downto 0) := x"80";
    signal mute_pipe_1, mute_pipe_2 : std_logic := '1';
    signal full_scale_pipe_1, full_scale_pipe_2 : std_logic := '0';

    signal adc_div_count : integer range 0 to 4 := 0;
    signal ad_clk_reg, sample_strobe : std_logic := '0';
    signal sample_signed : signed(8 downto 0);
    signal sample_raw : unsigned(7 downto 0);
    signal sample_otr, sample_valid : std_logic;
    signal ref_cos, ref_sin : signed(8 downto 0);

    signal start_measure, clear_result : std_logic;
    signal measure_sequence : unsigned(7 downto 0);
    signal measure_cycles, settle_ms : unsigned(15 downto 0);
    signal capture_en, clear_acc, latch_result : std_logic;
    signal measure_busy, measure_done, result_valid : std_logic;
    signal i_result, q_result : signed(63 downto 0);
    signal result_count : unsigned(31 downto 0);
    signal adc_min, adc_max : unsigned(7 downto 0);
    signal clip_detected, overflow_seen : std_logic;
begin
    u_pll : pll_125m
        port map (areset => not rst_n, inclk0 => clk, c0 => clk_core, locked => pll_locked);

    process(clk_core, rst_n, pll_locked)
    begin
        if rst_n = '0' or pll_locked = '0' then
            reset_pipe <= (others => '0');
        elsif rising_edge(clk_core) then
            reset_pipe <= reset_pipe(0) & '1';
        end if;
    end process;
    core_rst_n <= reset_pipe(1);

    u_da_clk_ddio : altddio_out
        generic map (intended_device_family => "Cyclone IV E", width => 1)
        port map (
            aclr => not core_rst_n, aset => '0', datain_h => "0", datain_l => "1",
            dataout => da_clk_ddio, oe => '1', oe_out => open, outclock => clk_core,
            outclocken => '1', sclr => '0', sset => '0');
    da_clk <= da_clk_ddio(0);
    da_data <= da_data_reg;

    -- 125 MHz / 5 = 25 MHz ADC clock.  The 16 ns high and 24 ns low times
    -- both satisfy the AD9280 clock pulse-width requirement.  Data is captured
    -- on the low-to-high transition; the previous conversion is then stable.
    process(clk_core, core_rst_n)
    begin
        if core_rst_n = '0' then
            adc_div_count <= 0;
            ad_clk_reg <= '0';
            sample_strobe <= '0';
        elsif rising_edge(clk_core) then
            sample_strobe <= '0';
            if adc_div_count = 4 then
                adc_div_count <= 0;
                ad_clk_reg <= '1';
                sample_strobe <= '1';
            else
                adc_div_count <= adc_div_count + 1;
                if adc_div_count = 1 then
                    ad_clk_reg <= '0';
                end if;
            end if;
        end if;
    end process;
    ad_clk_out <= ad_clk_reg;

    process(clk_core, core_rst_n)
        variable centered_sample : signed(8 downto 0);
        variable gain_signed : signed(16 downto 0);
        variable scaled_value : integer range -256 to 511;
        variable phase_sum : unsigned(PHASE_WIDTH downto 0);
    begin
        if core_rst_n = '0' then
            phase_acc <= (others => '0');
            phase_wrap <= '0';
            freq_word <= FREQ_MIN;
            da_data_reg <= x"80";
            centered_sample_pipe <= (others => '0');
            gain_pipe <= (others => '0');
            scaled_product_pipe <= (others => '0');
            bypass_sample_pipe_1 <= x"80";
            bypass_sample_pipe_2 <= x"80";
            mute_pipe_1 <= '1'; mute_pipe_2 <= '1';
            full_scale_pipe_1 <= '0'; full_scale_pipe_2 <= '0';
        elsif rising_edge(clk_core) then
            -- Register both the command clamp and the carry bit.  This removes
            -- the former clamp + 48-bit add + wrap comparator path from the
            -- measurement controller's single-cycle timing cone.
            if freq_word_cmd < FREQ_MIN then
                freq_word <= FREQ_MIN;
            elsif freq_word_cmd > FREQ_MAX then
                freq_word <= FREQ_MAX;
            else
                freq_word <= freq_word_cmd;
            end if;
            phase_sum := ('0' & phase_acc) + ('0' & freq_word);
            phase_acc <= phase_sum(PHASE_WIDTH - 1 downto 0);
            phase_wrap <= phase_sum(PHASE_WIDTH);
            centered_sample := signed('0' & rom_data) - to_signed(128, 9);
            gain_signed := signed('0' & std_logic_vector(amplitude_q16));
            centered_sample_pipe <= centered_sample;
            gain_pipe <= gain_signed;
            bypass_sample_pipe_1 <= rom_data;
            if amplitude_q16 = to_unsigned(0, 16) then mute_pipe_1 <= '1';
            else mute_pipe_1 <= '0'; end if;
            if amplitude_q16 = to_unsigned(65535, 16) then full_scale_pipe_1 <= '1';
            else full_scale_pipe_1 <= '0'; end if;

            scaled_product_pipe <= centered_sample_pipe * gain_pipe;
            bypass_sample_pipe_2 <= bypass_sample_pipe_1;
            mute_pipe_2 <= mute_pipe_1;
            full_scale_pipe_2 <= full_scale_pipe_1;

            if mute_pipe_2 = '1' then
                da_data_reg <= x"80";
            elsif full_scale_pipe_2 = '1' then
                da_data_reg <= bypass_sample_pipe_2;
            else
                scaled_value := 128 + to_integer(shift_right(scaled_product_pipe, 16));
                if scaled_value < 0 then da_data_reg <= x"00";
                elsif scaled_value > 255 then da_data_reg <= x"FF";
                else da_data_reg <= std_logic_vector(to_unsigned(scaled_value, 8)); end if;
            end if;
        end if;
    end process;

    rom_addr <= std_logic_vector(phase_acc(PHASE_WIDTH - 1 downto PHASE_WIDTH - 8));
    u_da_rom : sine_rom port map (address => rom_addr, clock => clk_core, q => rom_data);

    u_spi : entity work.spi_measure_if
        generic map (PHASE_WIDTH => PHASE_WIDTH)
        port map (
            clk => clk_core, rst_n => core_rst_n, spi_cs_n => spi_cs_n,
            spi_sck => spi_sck, spi_mosi => spi_mosi, spi_miso => spi_miso,
            measure_busy => measure_busy, measure_done => measure_done,
            result_valid => result_valid, clip_detected => clip_detected,
            overflow_seen => overflow_seen, i_result => i_result, q_result => q_result,
            sample_count => result_count, adc_min => adc_min, adc_max => adc_max,
            freq_word => freq_word_cmd, amplitude_q16 => amplitude_q16,
            measure_sequence => measure_sequence, measure_cycles => measure_cycles,
            settle_ms => settle_ms, start_measure => start_measure,
            clear_result => clear_result);

    u_ctrl : entity work.measure_ctrl
        generic map (CLOCK_HZ => 125000000)
        port map (
            clk => clk_core, rst_n => core_rst_n, start => start_measure,
            clear_result => clear_result, phase_wrap => phase_wrap,
            requested_cycles => measure_cycles, settle_ms => settle_ms,
            capture_en => capture_en, clear_acc => clear_acc,
            latch_result => latch_result, busy => measure_busy,
            done => measure_done, result_valid => result_valid);

    u_capture : entity work.adc_capture
        port map (
            clk => clk_core, rst_n => core_rst_n, capture_en => capture_en,
            sample_strobe => sample_strobe, ad_data => ad_data, ad_otr => '0',
            sample_signed => sample_signed, sample_raw => sample_raw,
            sample_otr => sample_otr, sample_valid => sample_valid);

    u_ref : entity work.adc_phase_ref
        generic map (PHASE_WIDTH => PHASE_WIDTH)
        port map (
            clk => clk_core, rst_n => core_rst_n, capture_en => capture_en,
            sample_strobe => sample_strobe, phase_word => phase_acc,
            ref_cos => ref_cos, ref_sin => ref_sin);

    u_iq : entity work.iq_demod
        port map (
            clk => clk_core, rst_n => core_rst_n, clear_acc => clear_acc,
            latch_result => latch_result, sample_valid => sample_valid,
            sample_signed => sample_signed, sample_raw => sample_raw,
            ref_cos => ref_cos, ref_sin => ref_sin, ad_otr => sample_otr,
            i_result => i_result, q_result => q_result, sample_count => result_count,
            adc_min => adc_min, adc_max => adc_max, clip_detected => clip_detected,
            overflow_seen => overflow_seen);
end rtl;
