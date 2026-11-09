# WhoScored Match Import

PitchRate can create a match from a WhoScored match page. The import is always
explicit: PitchRate downloads the page, parses the embedded match data, shows a
preview, and writes nothing until the user confirms.

## User flow

1. Open **Matches** and choose **Import from WhoScored**.
2. Paste a match URL such as
   `https://fr.whoscored.com/matches/1984066/live/france-ligue-1-2026-2027-rennes-paris-saint-germain`.
3. PitchRate downloads that page and opens the import preview.
4. Review and edit the date, clubs, score, player names, minutes and
   performance values. The neutral-venue option defaults to off.
5. Choose what to do for players who already exist in another club.
6. Choose **Import match**. PitchRate creates any missing clubs and players and
   records the match in one transaction.

Cancelling the preview leaves the database unchanged.

## What is imported

The page must contain WhoScored's embedded `matchCentreData`. PitchRate reads:

- the fixture date from `startDate` (or `startTime`);
- the full home and away club names from the match header when it provides a
  fuller name than `matchCentreData`, otherwise the names in `matchCentreData`;
- the final or current score from the full-time score, or from the score text;
- every player who appeared, with minutes derived from the starting line-up and
  substitution minutes. Unused substitutes are omitted.

Normal matches use a 90-minute length. Matches with an extra-time score use 120
minutes. A substitute who comes on at minute 90 is recorded with one minute.
Minutes are clamped to the 1-120 range accepted by PitchRate. The imported
performance defaults to 6.00 and can be edited before import.

Player names are split at the first space: the first token becomes the first
name and the remainder becomes the last name. A single-word name is stored as a
first name. Both fields are editable in the preview.

New players start at the default rating of 50.0. Existing players keep their
stored name, starting rating, status and history.

## Club matching

Club names are trimmed and matched to existing clubs ASCII case-insensitively.
If a name does not match, the club is created when the import is confirmed, with
an empty short name and city. The club fields in the preview are editable, so a
user can correct a scraped name to an existing club before importing.

## Player cues and decisions

Each imported player has a coloured cue and an action:

| Cue | Meaning | Default action |
| --- | --- | --- |
| Blue, "New player" | No player with this full name exists. | Create a new player. |
| Green, "In this squad" | The matching player already belongs to the imported club and is available. | Use the existing player. |
| Amber, club name | The matching player belongs to another club and is available. | Create a separate player. |
| Red, "Archived: club" | The matching player is archived. | Create a separate player. |

For an amber row, select the row and choose **Change player action** (or
double-click the cue/action cell). The choices are:

- **Add separate player**: create a new player in the imported club. The
  existing player and their history stay unchanged.
- **Move existing player**: move the matching player to the imported club. Their
  name, starting rating, availability and match history are preserved.

For a green row, the same action chooser offers **Add separate player** or
**Use existing player**, so two different people with the same name can remain
separate records.

The cue is recomputed whenever a first name, last name or club field changes. If
correcting a name creates a match with a player in another club, the row turns
amber and the action button becomes available immediately. Archived players
cannot be used in a new match.

If the same existing player would be used twice in the imported line-up, the
import is rejected with a validation error.

## Network behaviour and limits

PitchRate uses the Windows WinHTTP library to download the exact URL entered.
The URL must use HTTP or HTTPS and point at a `whoscored.com` match path.
PitchRate adds no cookies, credentials or proxy credentials, does not run
JavaScript, and does not intentionally contact any other site. No local data is
uploaded.

WhoScored can protect its pages with a browser check or rate limiting. PitchRate
does not run JavaScript and cannot complete a browser challenge. A blocked
request keeps the URL dialog open and explains the HTTP status, for example:

```text
WhoScored refused the request (HTTP 403). The site may be blocking automated
access. Try again later or use a normal browser to check the link.
```

The import also fails with a clear message if the page does not contain
`matchCentreData`, is not UTF-8, is larger than 16 MiB, or has no usable date,
teams, score or player minutes.

Only the explicit download started by the user performs network access. Imported
data is stored locally with the rest of the PitchRate data file.
