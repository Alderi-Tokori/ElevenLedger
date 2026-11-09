# WhoScored Match Import

PitchRate imports a match from the HTML source of a WhoScored match page. The
import is always explicit: source is pasted from the clipboard, parsed locally,
shown in a preview, and nothing is written until the user confirms. PitchRate
does not make network requests.

## User flow

1. Open the WhoScored match page in your browser.
2. Press **Ctrl+U** to view the page source.
3. Press **Ctrl+A**, then **Ctrl+C**.
4. In PitchRate, open **Matches** and choose **Import from WhoScored**.
5. Choose **Paste page source**.
6. Review and edit the date, clubs, score, player names, minutes and performance
   values. The neutral-venue option defaults to off.
7. Choose what to do for players who already exist in another club.
8. Choose **Import match**. PitchRate creates any missing clubs and players and
   records the match in one transaction.

Cancelling the preview leaves the database unchanged.

### Getting the source

Copying the rendered page with Ctrl+A/C on the normal page is not enough because
it produces visible text rather than HTML source. Use **View Source** (Ctrl+U)
instead. If Ctrl+U is unavailable, save the page with **Ctrl+S**, open the saved
`.html` file in a text editor, and copy all of it.

Direct HTTP requests to WhoScored are blocked by Cloudflare. PitchRate therefore
has no URL downloader and does not try to bypass the block. The browser-assisted
source paste is the supported import path. If your browser also shows a
Cloudflare block page, the block is coming from your network or IP address; use
a different network or contact WhoScored with the Ray ID shown on their block
page.

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

## Source requirements and privacy

The pasted source must contain WhoScored's embedded `matchCentreData`, be valid
UTF-8, and be no larger than 16 MiB. The import fails with a clear message if the
source is only rendered page text, uses an unsupported page layout, or has no
usable date, teams, score or player minutes.

The clipboard text is read only when the user chooses **Paste page source**. It
is parsed in memory, then discarded. PitchRate makes no network requests and
stores nothing from the source except the confirmed match, clubs and players in
the local data file.
