"""Autofocus experiments on the S5C73M3 power-on sequence (s5c73m3.ko variants).

usage: s5c-variant.py <kernel tree> <A|B|C>
  A: 10 ms after enabling all supplies, before the clock and the ISP start
  B: Samsung's Galaxy Note II order and delays (midas-camera.c, CONFIG_MACH_T0)
  C: B plus the ISP core at 1.15 V (Samsung's default; mainline runs it at 1.2 V)
  D: B's supply order only: no delays, the clock after all supplies as in mainline
  E: mainline's order with only the lens supply (vdd-af) moved before the I/O supplies
"""
import sys

tree, variant = sys.argv[1], sys.argv[2]
p = tree + "/drivers/media/i2c/s5c73m3/s5c73m3-core.c"
s = open(p).read()

old = """static int __s5c73m3_power_on(struct s5c73m3 *state)
{
	int i, ret;

	for (i = 0; i < S5C73M3_MAX_SUPPLIES; i++) {
		ret = regulator_enable(state->supplies[i].consumer);
		if (ret)
			goto err_reg_dis;
	}

	ret = clk_prepare_enable(state->clock);
	if (ret < 0)
		goto err_reg_dis;
"""
assert s.count(old) == 1

if variant in "DE":
    new = """static int __s5c73m3_power_on(struct s5c73m3 *state)
{
	static const int order[] = %s;
	int i, ret;

	pr_info("S5C73M3: power-on experiment %s (supply order only)\\n");
	for (i = 0; i < S5C73M3_MAX_SUPPLIES; i++) {
		ret = regulator_enable(state->supplies[order[i]].consumer);
		if (ret)
			goto err_seq;
	}
	goto seq_done;
err_seq:
	for (--i; i >= 0; i--)
		regulator_disable(state->supplies[order[i]].consumer);
	return ret;
seq_done:
	i = S5C73M3_MAX_SUPPLIES;

	ret = clk_prepare_enable(state->clock);
	if (ret < 0)
		goto err_reg_dis;
""" % ("{ 0, 2, 1, 5, 3, 4 }" if variant == "D" else "{ 0, 1, 2, 5, 3, 4 }", variant)
elif variant == "A":
    new = """static int __s5c73m3_power_on(struct s5c73m3 *state)
{
	int i, ret;

	pr_info("S5C73M3: power-on experiment A (10 ms after the supplies)\\n");
	for (i = 0; i < S5C73M3_MAX_SUPPLIES; i++) {
		ret = regulator_enable(state->supplies[i].consumer);
		if (ret)
			goto err_reg_dis;
	}
	usleep_range(10000, 11000);

	ret = clk_prepare_enable(state->clock);
	if (ret < 0)
		goto err_reg_dis;
"""
else:
    vcore = """
	/* experiment C: Samsung's default ISP core voltage */
	ret = regulator_set_voltage(state->supplies[0].consumer, 1150000, 1150000);
	pr_info("S5C73M3: ISP core 1.15 V: %d\\n", ret);
""" if variant == "C" else ""
    new = """static int __s5c73m3_power_on(struct s5c73m3 *state)
{
	/*
	 * experiment %s: Samsung's T0 sequence - ISP core, CAM_IO (vdd-reg), sensor core (vdda)
	 * +200 us, MCLK, CAM_AF +2 ms, sensor I/O (vddio-host), MIPI (vddio-cis) +5 ms
	 */
	static const int order[] = { 0, 2, 1, 5, 3, 4 };
	static const unsigned int delay_us[] = { 0, 0, 200, 2000, 0, 5000 };
	bool clk_on = false;
	int i, ret;

	pr_info("S5C73M3: power-on experiment %s (Samsung T0 sequence)\\n");%s
	for (i = 0; i < ARRAY_SIZE(order); i++) {
		if (order[i] == 5) {
			ret = clk_prepare_enable(state->clock);
			if (ret < 0)
				goto err_seq;
			clk_on = true;
		}
		ret = regulator_enable(state->supplies[order[i]].consumer);
		if (ret)
			goto err_seq;
		if (delay_us[i])
			usleep_range(delay_us[i], delay_us[i] + 100);
	}
	goto seq_done;
err_seq:
	if (clk_on)
		clk_disable_unprepare(state->clock);
	for (--i; i >= 0; i--)
		regulator_disable(state->supplies[order[i]].consumer);
	return ret;
seq_done:
	i = S5C73M3_MAX_SUPPLIES;
	if (!clk_on)	/* keeps err_reg_dis referenced */
		goto err_reg_dis;
""" % (variant, variant, vcore)

s = s.replace(old, new)
open(p, "w").write(s)
print("variant", variant, "applied")
