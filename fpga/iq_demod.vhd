library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

entity iq_demod is
    port (
        clk             : in  std_logic;
        rst_n           : in  std_logic;
        clear_acc       : in  std_logic;
        latch_result    : in  std_logic;
        sample_valid    : in  std_logic;
        sample_signed   : in  signed(8 downto 0);
        sample_raw      : in  unsigned(7 downto 0);
        ref_cos         : in  signed(8 downto 0);
        ref_sin         : in  signed(8 downto 0);
        ad_otr          : in  std_logic;
        i_result        : out signed(63 downto 0);
        q_result        : out signed(63 downto 0);
        sample_count    : out unsigned(31 downto 0);
        adc_min         : out unsigned(7 downto 0);
        adc_max         : out unsigned(7 downto 0);
        clip_detected   : out std_logic;
        overflow_seen   : out std_logic
    );
end entity;

architecture rtl of iq_demod is
    signal product_i       : signed(17 downto 0) := (others => '0');
    signal product_q       : signed(17 downto 0) := (others => '0');
    signal product_valid   : std_logic := '0';
    signal product_raw     : unsigned(7 downto 0) := (others => '0');
    signal product_otr     : std_logic := '0';
    signal i_acc           : signed(63 downto 0) := (others => '0');
    signal q_acc           : signed(63 downto 0) := (others => '0');
    signal count_acc       : unsigned(31 downto 0) := (others => '0');
    signal min_acc         : unsigned(7 downto 0) := (others => '1');
    signal max_acc         : unsigned(7 downto 0) := (others => '0');
    signal clip_acc        : std_logic := '0';
    signal overflow_acc    : std_logic := '0';
    signal overflow_check_valid : std_logic := '0';
    signal i_old_sign, i_add_sign : std_logic := '0';
    signal q_old_sign, q_add_sign : std_logic := '0';
begin
    process(clk, rst_n)
        variable add_i : signed(63 downto 0);
        variable add_q : signed(63 downto 0);
        variable next_i : signed(63 downto 0);
        variable next_q : signed(63 downto 0);
    begin
        if rst_n = '0' then
            product_i      <= (others => '0');
            product_q      <= (others => '0');
            product_valid  <= '0';
            product_raw    <= (others => '0');
            product_otr    <= '0';
            i_acc          <= (others => '0');
            q_acc          <= (others => '0');
            count_acc      <= (others => '0');
            min_acc        <= (others => '1');
            max_acc        <= (others => '0');
            clip_acc       <= '0';
            overflow_acc   <= '0';
            overflow_check_valid <= '0';
            i_old_sign <= '0';
            i_add_sign <= '0';
            q_old_sign <= '0';
            q_add_sign <= '0';
            i_result       <= (others => '0');
            q_result       <= (others => '0');
            sample_count   <= (others => '0');
            adc_min        <= (others => '0');
            adc_max        <= (others => '0');
            clip_detected  <= '0';
            overflow_seen  <= '0';
        elsif rising_edge(clk) then
            product_valid <= sample_valid;
            overflow_check_valid <= '0';
            if sample_valid = '1' then
                product_i   <= sample_signed * ref_cos;
                product_q   <= sample_signed * ref_sin;
                product_raw <= sample_raw;
                product_otr <= ad_otr;
            end if;

            if clear_acc = '1' then
                i_acc         <= (others => '0');
                q_acc         <= (others => '0');
                count_acc     <= (others => '0');
                min_acc       <= (others => '1');
                max_acc       <= (others => '0');
                clip_acc      <= '0';
                overflow_acc  <= '0';
                product_valid <= '0';
                overflow_check_valid <= '0';
            elsif product_valid = '1' then
                add_i := resize(product_i, 64);
                add_q := resize(product_q, 64);
                next_i := i_acc + add_i;
                next_q := q_acc + add_q;
                i_acc <= next_i;
                q_acc <= next_q;

                -- Products arrive once every five core clocks.  Delay the
                -- sign comparison by one clock so the overflow flag is not
                -- behind the complete 64-bit carry chain.
                i_old_sign <= i_acc(63);
                i_add_sign <= add_i(63);
                q_old_sign <= q_acc(63);
                q_add_sign <= add_q(63);
                overflow_check_valid <= '1';

                if count_acc /= x"FFFFFFFF" then
                    count_acc <= count_acc + 1;
                else
                    overflow_acc <= '1';
                end if;
                if product_raw < min_acc then
                    min_acc <= product_raw;
                end if;
                if product_raw > max_acc then
                    max_acc <= product_raw;
                end if;
                if product_otr = '1' or product_raw <= to_unsigned(1, 8) or
                   product_raw >= to_unsigned(254, 8) then
                    clip_acc <= '1';
                end if;
            end if;

            if overflow_check_valid = '1' and clear_acc = '0' then
                if (i_old_sign = i_add_sign and i_acc(63) /= i_old_sign) or
                   (q_old_sign = q_add_sign and q_acc(63) /= q_old_sign) then
                    overflow_acc <= '1';
                end if;
            end if;

            if latch_result = '1' then
                i_result       <= i_acc;
                q_result       <= q_acc;
                sample_count   <= count_acc;
                adc_min        <= min_acc;
                adc_max        <= max_acc;
                clip_detected  <= clip_acc;
                overflow_seen  <= overflow_acc;
            end if;
        end if;
    end process;
end rtl;
