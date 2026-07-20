library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity adc_phase_ref is
    generic (
        PHASE_WIDTH : positive := 48
    );
    port (
        clk           : in  std_logic;
        rst_n         : in  std_logic;
        capture_en    : in  std_logic;
        sample_strobe : in  std_logic;
        phase_word    : in  unsigned(PHASE_WIDTH - 1 downto 0);
        ref_cos       : out signed(8 downto 0);
        ref_sin       : out signed(8 downto 0)
    );
end entity;

architecture rtl of adc_phase_ref is
    component sine_rom is
        port (
            address : in  std_logic_vector(7 downto 0);
            clock   : in  std_logic := '1';
            q       : out std_logic_vector(7 downto 0)
        );
    end component;

    signal sin_addr : std_logic_vector(7 downto 0);
    signal cos_addr : std_logic_vector(7 downto 0);
    signal sin_data : std_logic_vector(7 downto 0);
    signal cos_data : std_logic_vector(7 downto 0);
begin
    sin_addr <= std_logic_vector(phase_word(PHASE_WIDTH - 1 downto PHASE_WIDTH - 8));
    cos_addr <= std_logic_vector(unsigned(sin_addr) + to_unsigned(64, 8));

    u_sin_rom : sine_rom
        port map (address => sin_addr, clock => clk, q => sin_data);

    u_cos_rom : sine_rom
        port map (address => cos_addr, clock => clk, q => cos_data);

    process(clk, rst_n)
    begin
        if rst_n = '0' then
            ref_sin <= (others => '0');
            ref_cos <= (others => '0');
        elsif rising_edge(clk) then
            if capture_en = '1' and sample_strobe = '1' then
                ref_sin <= signed('0' & sin_data) - to_signed(128, 9);
                ref_cos <= signed('0' & cos_data) - to_signed(128, 9);
            end if;
        end if;
    end process;
end rtl;
