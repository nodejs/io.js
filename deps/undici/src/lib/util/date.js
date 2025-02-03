'use strict'

const IMF_DAYS = ['mon', 'tue', 'wed', 'thu', 'fri', 'sat', 'sun']
const IMF_SPACES = [4, 7, 11, 16, 25]
const IMF_MONTHS = ['jan', 'feb', 'mar', 'apr', 'may', 'jun', 'jul', 'aug', 'sep', 'oct', 'nov', 'dec']
const IMF_COLONS = [19, 22]

const ASCTIME_SPACES = [3, 7, 10, 19]

const RFC850_DAYS = ['monday', 'tuesday', 'wednesday', 'thursday', 'friday', 'saturday', 'sunday']

/**
 * @see https://www.rfc-editor.org/rfc/rfc9110.html#name-date-time-formats
 *
 * @param {string} date
 * @param {Date} [now]
 * @returns {Date | undefined}
 */
function parseHttpDate (date, now) {
  // Sun, 06 Nov 1994 08:49:37 GMT    ; IMF-fixdate
  // Sun Nov  6 08:49:37 1994         ; ANSI C's asctime() format
  // Sunday, 06-Nov-94 08:49:37 GMT   ; obsolete RFC 850 format

  date = date.toLowerCase()

  switch (date[3]) {
    case ',': return parseImfDate(date)
    case ' ': return parseAscTimeDate(date)
    default: return parseRfc850Date(date, now)
  }
}

/**
 * @see https://httpwg.org/specs/rfc9110.html#preferred.date.format
 *
 * @param {string} date
 * @returns {Date | undefined}
 */
function parseImfDate (date) {
  if (date.length !== 29) {
    return undefined
  }

  if (!date.endsWith('gmt')) {
    // Unsupported timezone
    return undefined
  }

  for (const spaceInx of IMF_SPACES) {
    if (date[spaceInx] !== ' ') {
      return undefined
    }
  }

  for (const colonIdx of IMF_COLONS) {
    if (date[colonIdx] !== ':') {
      return undefined
    }
  }

  const dayName = date.substring(0, 3)
  if (!IMF_DAYS.includes(dayName)) {
    return undefined
  }

  const dayString = date.substring(5, 7)
  const day = Number.parseInt(dayString)
  if (isNaN(day) || (day < 10 && dayString[0] !== '0')) {
    // Not a number, 0, or it's less than 10 and didn't start with a 0
    return undefined
  }

  const month = date.substring(8, 11)
  const monthIdx = IMF_MONTHS.indexOf(month)
  if (monthIdx === -1) {
    return undefined
  }

  const year = Number.parseInt(date.substring(12, 16))
  if (isNaN(year)) {
    return undefined
  }

  const hourString = date.substring(17, 19)
  const hour = Number.parseInt(hourString)
  if (isNaN(hour) || (hour < 10 && hourString[0] !== '0')) {
    return undefined
  }

  const minuteString = date.substring(20, 22)
  const minute = Number.parseInt(minuteString)
  if (isNaN(minute) || (minute < 10 && minuteString[0] !== '0')) {
    return undefined
  }

  const secondString = date.substring(23, 25)
  const second = Number.parseInt(secondString)
  if (isNaN(second) || (second < 10 && secondString[0] !== '0')) {
    return undefined
  }

  return new Date(Date.UTC(year, monthIdx, day, hour, minute, second))
}

/**
 * @see https://httpwg.org/specs/rfc9110.html#obsolete.date.formats
 *
 * @param {string} date
 * @returns {Date | undefined}
 */
function parseAscTimeDate (date) {
  // This is assumed to be in UTC

  if (date.length !== 24) {
    return undefined
  }

  for (const spaceIdx of ASCTIME_SPACES) {
    if (date[spaceIdx] !== ' ') {
      return undefined
    }
  }

  const dayName = date.substring(0, 3)
  if (!IMF_DAYS.includes(dayName)) {
    return undefined
  }

  const month = date.substring(4, 7)
  const monthIdx = IMF_MONTHS.indexOf(month)
  if (monthIdx === -1) {
    return undefined
  }

  const dayString = date.substring(8, 10)
  const day = Number.parseInt(dayString)
  if (isNaN(day) || (day < 10 && dayString[0] !== ' ')) {
    return undefined
  }

  const hourString = date.substring(11, 13)
  const hour = Number.parseInt(hourString)
  if (isNaN(hour) || (hour < 10 && hourString[0] !== '0')) {
    return undefined
  }

  const minuteString = date.substring(14, 16)
  const minute = Number.parseInt(minuteString)
  if (isNaN(minute) || (minute < 10 && minuteString[0] !== '0')) {
    return undefined
  }

  const secondString = date.substring(17, 19)
  const second = Number.parseInt(secondString)
  if (isNaN(second) || (second < 10 && secondString[0] !== '0')) {
    return undefined
  }

  const year = Number.parseInt(date.substring(20, 24))
  if (isNaN(year)) {
    return undefined
  }

  return new Date(Date.UTC(year, monthIdx, day, hour, minute, second))
}

/**
 * @see https://httpwg.org/specs/rfc9110.html#obsolete.date.formats
 *
 * @param {string} date
 * @param {Date} [now]
 * @returns {Date | undefined}
 */
function parseRfc850Date (date, now = new Date()) {
  if (!date.endsWith('gmt')) {
    // Unsupported timezone
    return undefined
  }

  const commaIndex = date.indexOf(',')
  if (commaIndex === -1) {
    return undefined
  }

  if ((date.length - commaIndex - 1) !== 23) {
    return undefined
  }

  const dayName = date.substring(0, commaIndex)
  if (!RFC850_DAYS.includes(dayName)) {
    return undefined
  }

  if (
    date[commaIndex + 1] !== ' ' ||
    date[commaIndex + 4] !== '-' ||
    date[commaIndex + 8] !== '-' ||
    date[commaIndex + 11] !== ' ' ||
    date[commaIndex + 14] !== ':' ||
    date[commaIndex + 17] !== ':' ||
    date[commaIndex + 20] !== ' '
  ) {
    return undefined
  }

  const dayString = date.substring(commaIndex + 2, commaIndex + 4)
  const day = Number.parseInt(dayString)
  if (isNaN(day) || (day < 10 && dayString[0] !== '0')) {
    // Not a number, or it's less than 10 and didn't start with a 0
    return undefined
  }

  const month = date.substring(commaIndex + 5, commaIndex + 8)
  const monthIdx = IMF_MONTHS.indexOf(month)
  if (monthIdx === -1) {
    return undefined
  }

  // As of this point year is just the decade (i.e. 94)
  let year = Number.parseInt(date.substring(commaIndex + 9, commaIndex + 11))
  if (isNaN(year)) {
    return undefined
  }

  const currentYear = now.getUTCFullYear()
  const currentDecade = currentYear % 100
  const currentCentury = Math.floor(currentYear / 100)

  if (year > currentDecade && year - currentDecade >= 50) {
    // Over 50 years in future, go to previous century
    year += (currentCentury - 1) * 100
  } else {
    year += currentCentury * 100
  }

  const hourString = date.substring(commaIndex + 12, commaIndex + 14)
  const hour = Number.parseInt(hourString)
  if (isNaN(hour) || (hour < 10 && hourString[0] !== '0')) {
    return undefined
  }

  const minuteString = date.substring(commaIndex + 15, commaIndex + 17)
  const minute = Number.parseInt(minuteString)
  if (isNaN(minute) || (minute < 10 && minuteString[0] !== '0')) {
    return undefined
  }

  const secondString = date.substring(commaIndex + 18, commaIndex + 20)
  const second = Number.parseInt(secondString)
  if (isNaN(second) || (second < 10 && secondString[0] !== '0')) {
    return undefined
  }

  return new Date(Date.UTC(year, monthIdx, day, hour, minute, second))
}

module.exports = {
  parseHttpDate
}
