/*
 * MiniOS Real-Time Clock (CMOS RTC) Driver
 *
 * Reads the actual date and time from the CMOS RTC chip
 * via I/O ports 0x70 (address) and 0x71 (data).
 */

#include "../include/rtc.h"
#include "../include/io.h"
#include "../include/serial.h"
#include "../include/stdio.h"

/* CMOS I/O ports */
#define CMOS_ADDR  0x70
#define CMOS_DATA  0x71

/* CMOS RTC register indices */
#define RTC_SECONDS   0x00
#define RTC_MINUTES   0x02
#define RTC_HOURS     0x04
#define RTC_DAY       0x07
#define RTC_MONTH     0x08
#define RTC_YEAR      0x09
#define RTC_STATUS_A  0x0A
#define RTC_STATUS_B  0x0B

/*
 * Read a CMOS register
 */
static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    io_wait();
    return inb(CMOS_DATA);
}

/*
 * Check if the RTC is currently updating
 */
static bool rtc_is_updating(void) {
    return (cmos_read(RTC_STATUS_A) & 0x80) != 0;
}

/*
 * Convert BCD to binary
 */
static uint8_t bcd_to_bin(uint8_t bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

/*
 * Initialize the RTC driver
 */
void rtc_init(void) {
    rtc_time_t t;
    rtc_read_time(&t);
    printk("RTC: %04d-%02d-%02d %02d:%02d:%02d\n",
           t.year, t.month, t.day, t.hours, t.minutes, t.seconds);
}

/*
 * Read the current time from the CMOS RTC.
 * Reads twice and compares to avoid getting inconsistent data
 * during an RTC update cycle.
 */
void rtc_read_time(rtc_time_t* time) {
    uint8_t sec, min, hr, day, mon, yr;
    uint8_t sec2, min2, hr2, day2, mon2, yr2;
    uint8_t status_b;

    /* Wait until RTC is not updating */
    while (rtc_is_updating());

    /* First read */
    sec = cmos_read(RTC_SECONDS);
    min = cmos_read(RTC_MINUTES);
    hr  = cmos_read(RTC_HOURS);
    day = cmos_read(RTC_DAY);
    mon = cmos_read(RTC_MONTH);
    yr  = cmos_read(RTC_YEAR);

    /* Read again until we get two consistent reads */
    do {
        sec2 = sec; min2 = min; hr2 = hr;
        day2 = day; mon2 = mon; yr2 = yr;

        while (rtc_is_updating());

        sec = cmos_read(RTC_SECONDS);
        min = cmos_read(RTC_MINUTES);
        hr  = cmos_read(RTC_HOURS);
        day = cmos_read(RTC_DAY);
        mon = cmos_read(RTC_MONTH);
        yr  = cmos_read(RTC_YEAR);
    } while (sec != sec2 || min != min2 || hr != hr2 ||
             day != day2 || mon != mon2 || yr != yr2);

    /* Check status register B to determine data format */
    status_b = cmos_read(RTC_STATUS_B);

    /* Convert BCD to binary if needed (bit 2 of status B = 0 means BCD) */
    if (!(status_b & 0x04)) {
        sec = bcd_to_bin(sec);
        min = bcd_to_bin(min);
        hr  = bcd_to_bin(hr & 0x7F) | (hr & 0x80);  /* Preserve AM/PM bit */
        day = bcd_to_bin(day);
        mon = bcd_to_bin(mon);
        yr  = bcd_to_bin(yr);
    }

    /* Convert 12-hour to 24-hour if needed (bit 1 of status B = 0 means 12h) */
    if (!(status_b & 0x02) && (hr & 0x80)) {
        hr = ((hr & 0x7F) % 12) + 12;
    }

    time->seconds = sec;
    time->minutes = min;
    time->hours   = hr;
    time->day     = day;
    time->month   = mon;
    time->year    = 2000 + yr;  /* CMOS year is 0-99, assume 2000s */
}

/* Timezone offset in minutes from UTC (default 0 = UTC) */
static int tz_offset_minutes = 0;

/* Manual time offset in seconds */
static int manual_offset_seconds = 0;

void rtc_set_tz_offset(int offset_minutes) {
    tz_offset_minutes = offset_minutes;
}

int rtc_get_tz_offset(void) {
    return tz_offset_minutes;
}

void rtc_set_manual_offset(int offset_seconds) {
    manual_offset_seconds = offset_seconds;
}

int rtc_get_manual_offset(void) {
    return manual_offset_seconds;
}

/* Days in each month (non-leap) */
static const uint8_t days_in_month[] = {
    0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static bool is_leap_year(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static uint8_t month_days(int month, int year) {
    if (month == 2 && is_leap_year(year)) return 29;
    if (month >= 1 && month <= 12) return days_in_month[month];
    return 30;
}

/*
 * Read the adjusted time (RTC + timezone + manual offset)
 */
void rtc_get_adjusted_time(rtc_time_t* time) {
    rtc_read_time(time);

    /* Total offset in seconds */
    int total_offset = tz_offset_minutes * 60 + manual_offset_seconds;
    if (total_offset == 0) return;

    /* Convert current time to total seconds since midnight */
    int secs = (int)time->hours * 3600 + (int)time->minutes * 60 + (int)time->seconds;
    secs += total_offset;

    /* Handle day rollover */
    int day_delta = 0;
    while (secs >= 86400) { secs -= 86400; day_delta++; }
    while (secs < 0)      { secs += 86400; day_delta--; }

    time->hours   = (uint8_t)(secs / 3600);
    time->minutes = (uint8_t)((secs % 3600) / 60);
    time->seconds = (uint8_t)(secs % 60);

    /* Adjust date if day rolled over */
    if (day_delta != 0) {
        int d = time->day;
        int m = time->month;
        int y = time->year;

        d += day_delta;
        while (d > month_days(m, y)) {
            d -= month_days(m, y);
            m++;
            if (m > 12) { m = 1; y++; }
        }
        while (d < 1) {
            m--;
            if (m < 1) { m = 12; y--; }
            d += month_days(m, y);
        }

        time->day   = (uint8_t)d;
        time->month = (uint8_t)m;
        time->year  = (uint16_t)y;
    }
}

uint8_t rtc_get_hours(void) {
    rtc_time_t t;
    rtc_get_adjusted_time(&t);
    return t.hours;
}

uint8_t rtc_get_minutes(void) {
    rtc_time_t t;
    rtc_get_adjusted_time(&t);
    return t.minutes;
}

uint8_t rtc_get_seconds(void) {
    rtc_time_t t;
    rtc_get_adjusted_time(&t);
    return t.seconds;
}
