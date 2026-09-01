#pragma once
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class LowPassFilter {
	bool has_last;
	double y_prev;

public:
	LowPassFilter() : has_last(false), y_prev(0.0) {}

	double filter(double value, double alpha) {
		if (!has_last) {
			y_prev = value;
			has_last = true;
			return value;
		}
		y_prev = alpha * value + (1.0 - alpha) * y_prev;
		return y_prev;
	}

	void reset() {
		has_last = false;
		y_prev = 0.0;
	}
};

class OneEuroFilter {
	double min_cutoff;
	double beta;
	double d_cutoff;
	LowPassFilter x_filter;
	LowPassFilter dx_filter;
	bool has_last_time;
	double last_time;

	double alpha(double rate, double cutoff) {
		double tau = 1.0 / (2.0 * M_PI * cutoff);
		double te = 1.0 / rate;
		return 1.0 / (1.0 + tau / te);
	}

public:
	OneEuroFilter(double min_cutoff = 1.0, double beta = 0.007, double d_cutoff = 1.0)
		: min_cutoff(min_cutoff), beta(beta), d_cutoff(d_cutoff), has_last_time(false), last_time(0.0) {}

	void reset() {
		has_last_time = false;
		x_filter.reset();
		dx_filter.reset();
	}

	double filter(double value, double timestamp) {
		if (!has_last_time) {
			has_last_time = true;
			last_time = timestamp;
			return x_filter.filter(value, 1.0);
		}

		double dt = timestamp - last_time;
		last_time = timestamp;
		if (dt <= 0.0)
			dt = 1.0 / 60.0;

		double rate = 1.0 / dt;
		double prev_x = x_filter.filter(value, 1.0); // Peek or estimate dx
		double dx = (value - prev_x) * rate;
		double edx = dx_filter.filter(dx, alpha(rate, d_cutoff));
		double cutoff = min_cutoff + beta * std::abs(edx);
		return x_filter.filter(value, alpha(rate, cutoff));
	}
};

struct RectFilter {
	OneEuroFilter x0;
	OneEuroFilter y0;
	OneEuroFilter x1;
	OneEuroFilter y1;

	void reset() {
		x0.reset();
		y0.reset();
		x1.reset();
		y1.reset();
	}

	void filter_rect(int &rx0, int &ry0, int &rx1, int &ry1, double timestamp) {
		rx0 = (int)std::round(x0.filter((double)rx0, timestamp));
		ry0 = (int)std::round(y0.filter((double)ry0, timestamp));
		rx1 = (int)std::round(x1.filter((double)rx1, timestamp));
		ry1 = (int)std::round(y1.filter((double)ry1, timestamp));
	}
};
