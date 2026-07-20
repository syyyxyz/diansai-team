library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity adc_capture is
    port (
        clk           : in  std_logic;
        rst_n         : in  std_logic;
        capture_en    : in  std_logic;
        sample_strobe : in  std_logic;
        ad_data       : in  std_logic_vector(7 downto 0);
        ad_otr        : in  std_logic;
        sample_signed : out signed(8 downto 0);
        sample_raw    : out unsigned(7 downto 0);
        sample_otr    : out std_logic;
        sample_valid  : out std_logic
    );
end entity;

architecture rtl of adc_capture is
begin
    process(clk, rst_n)
    begin
        if rst_n = '0' then
            sample_signed <= (others => '0');
            sample_raw    <= (others => '0');
            sample_otr    <= '0';
            sample_valid  <= '0';
        elsif rising_edge(clk) then
            sample_valid <= '0';
            if capture_en = '1' and sample_strobe = '1' then
                sample_raw    <= unsigned(ad_data);
                sample_signed <= signed('0' & ad_data) - to_signed(128, 9);
                sample_otr    <= ad_otr;
                sample_valid  <= '1';
            end if;
        end if;
    end process;
end rtl;
