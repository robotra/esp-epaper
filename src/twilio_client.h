#pragma once
#include <Arduino.h>

// ============================================================
//  twilio_client.h — Twilio REST API: fetch recent SMS messages
//
//  Uses the Twilio Messages resource:
//    GET https://api.twilio.com/2010-04-01/Accounts/{SID}/Messages.json
//          ?To={number}&PageSize={n}
//
//  Authentication is HTTP Basic Auth with the Account SID as the
//  username and the Auth Token as the password.  The credentials
//  are Base64-encoded and sent in the Authorization header as:
//    "Basic <base64(AccountSid:AuthToken)>"
//
//  The `To` parameter filters messages to only those received by
//  the specified Twilio number.  Results are sorted newest-first
//  by Twilio's default ordering.
//
//  Optional whitelist:
//    If `fromWhitelist` is non-empty, only messages whose `from`
//    field matches one of the comma-separated E.164 numbers in the
//    whitelist are returned.  To ensure we still get `maxCount`
//    results after filtering, the API is queried with a larger
//    page size (up to 3×, capped at 50).
// ============================================================


// Hard cap on the messages array passed to twilioFetchMessages().
// The caller's array must be at least this size; the `maxCount`
// argument further limits how many entries are actually filled.
#define TWILIO_MSG_COUNT_MAX 10


// ------------------------------------------------------------
//  TwilioMessage — one SMS message record
//
//  Fields:
//    from     — sender's phone number in E.164 format (+15550001234)
//    body     — UTF-8 message text (may contain emoji/Unicode)
//    dateSent — ISO 8601 timestamp string from Twilio, e.g.
//               "Wed, 17 Mar 2026 14:32:00 +0000"
//               ui_renderer.cpp slices characters 5–21 to show
//               a compact "DD Mon HH:MM" display.
// ------------------------------------------------------------
struct TwilioMessage {
    String from;
    String body;
    String dateSent;
};


// ------------------------------------------------------------
//  twilioFetchMessages — retrieve recent inbound SMS messages
//
//  Parameters:
//    accountSid    — Twilio Account SID (starts with "AC")
//    authToken     — Twilio Auth Token (secret)
//    toNumber      — E.164 number to fetch messages for, e.g. "+15550001234"
//                    (the '+' is URL-encoded to "%2B" internally)
//    fromWhitelist — comma-separated E.164 senders to accept, or
//                    NULL / empty string to accept any sender
//    out[]         — caller-allocated array of size ≥ maxCount
//    maxCount      — maximum number of messages to return
//                    (must be ≤ TWILIO_MSG_COUNT_MAX)
//    errorOut      — optional pointer to a String; if non-null and an
//                    error occurs, a human-readable description is written
//                    here (e.g. "HTTP 403", "JSON parse error").
//                    Untouched on success.
//
//  Returns:
//    ≥ 0  — number of TwilioMessage entries written to out[]
//    -1   — HTTP error or JSON parse failure (see errorOut)
// ------------------------------------------------------------
int twilioFetchMessages(const char *accountSid,
                        const char *authToken,
                        const char *toNumber,
                        const char *fromWhitelist,
                        TwilioMessage out[],
                        int maxCount,
                        String *errorOut = nullptr);
