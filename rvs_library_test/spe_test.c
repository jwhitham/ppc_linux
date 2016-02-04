

float v_one = 1.0;

void spe_one (unsigned count)
{
	while (count) {
		v_one += 1.234567f;
		count --;
	}
}

double v_two = 1.0;

void spe_two (unsigned count)
{
	while (count) {
		v_two += 1.234567;
		count --;
	}
}

