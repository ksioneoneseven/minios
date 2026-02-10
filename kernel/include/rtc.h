/*
 * MiniOS Real-Time Clock (CMOS RTC) Driver
 *
 * Reads the actual date and time from the CMOS RTC chip.
 */

#ifndef _RTC_H
#define _RTC_H

#include "types.h"

/* Time structure */
typedef struct {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} rtc_time_t;

/*
 * Initialize the RTC driver
 */
void rtc_init(void);

/*
 * Read the current time from the CMOS RTC
 */
void rtc_read_time(rtc_time_t* time);

/*
 * Get hours (0-23) from RTC
 */
uint8_t rtc_get_hours(void);

/*
 * Get minutes (0-59) from RTC
 */
uint8_t rtc_get_minutes(void);

/*
 * Get seconds (0-59) from RTC
 */
uint8_t rtc_get_seconds(void);

/*
 * Set timezone offset in minutes from UTC (e.g. -300 for EST, +530 for IST)
 */
void rtc_set_tz_offset(int offset_minutes);

/*
 * Get current timezone offset in minutes from UTC
 */
int rtc_get_tz_offset(void);

/*
 * Set a manual time offset in seconds (added on top of RTC + timezone)
 */
void rtc_set_manual_offset(int offset_seconds);

/*
 * Get the manual time offset in seconds
 */
int rtc_get_manual_offset(void);

/*
 * Read the adjusted time (RTC + timezone + manual offset)
 */
void rtc_get_adjusted_time(rtc_time_t* time);

#endif /* _RTC_H */
