# WhoScored Match Import

ElevenLedger imports a match from the HTML source of a WhoScored match page. The
import is always explicit: source is pasted from the clipboard, parsed locally,
shown in a preview, and nothing is written until the user confirms. ElevenLedger
does not make network requests.

## User flow

1. Open the WhoScored match page in your browser.
2. Press **Ctrl+U** to view the page source.
3. Press **Ctrl+A**, then **Ctrl+C**.
4. In ElevenLedger, open **Matches** and choose **Import from WhoScored**.
5. Choose **Paste page source**.
6. Review and edit the date, clubs, score, player names, minutes and performance
   values. The neutral-venue option defaults to off. The live rating summary
   updates with these fields.
7. Choose what to do for players who already exist in another club.
8. Choose **Import match**. ElevenLedger creates any missing clubs and players and
   records the match in one transaction.

Cancelling the preview leaves the database unchanged.

### Getting the source

Copying the rendered page with Ctrl+A/C on the normal page is not enough because
it produces visible text rather than HTML source. Use **View Source** (Ctrl+U)
instead. If Ctrl+U is unavailable, save the page with **Ctrl+S**, open the saved
`.html` file in a text editor, and copy all of it.

Direct HTTP requests to WhoScored are blocked by Cloudflare. ElevenLedger therefore
has no URL downloader and does not try to bypass the block. The browser-assisted
source paste is the supported import path. If your browser also shows a
Cloudflare block page, the block is coming from your network or IP address; use
a different network or contact WhoScored with the Ray ID shown on their block
page.

## What is imported

The page must contain WhoScored's embedded `matchCentreData`. ElevenLedger reads:

- the fixture date from `startDate` (or `startTime`);
- the full home and away club names from the match header when it provides a
  fuller name than `matchCentreData`, otherwise the names in `matchCentreData`;
- the final or current score from the full-time score, or from the score text;
- every player who appeared, with actual minutes derived from the starting
  line-up, substitution minutes and sending-offs, plus the sending-off minute
  when applicable. Unused substitutes are omitted.
- each appearing player's final WhoScored match rating when the page provides
  one.
- the final whistle minute from the real elapsed match length.

ElevenLedger uses the real elapsed match length. It reads `maxMinute` (for example
94 for a match that ended at 90+4), falls back to the last recorded
`periodEndMinutes` entry, and finally falls back to 90 or 120 minutes when the
page has no timing data. `minuteExpanded` and `expandedMaxMinute` are not used
as the match length: WhoScored's expanded clock adds the first-half stoppage
offset back into the second half, so it runs ahead of the real match clock.

Starting players receive the full match length. Substitution times are converted
from the expanded clock back to the real match clock before minutes are
calculated. A substitute receives the time between their entry and the end of
the match, or their own substitution-out time when they are replaced again.
A substitute recorded as entering and leaving in the same minute, or who enters
during stoppage time, is recorded with at least one minute. A substitution time
reported beyond the real final whistle is capped at the final whistle so the
appearance cannot exceed the match length. Straight red cards
and second yellows are read from the match events and stop that player's actual
minutes at the sending-off minute. Cards and players are matched by WhoScored's
numeric `playerId`, whatever its size. A card recorded
for a player who had already been substituted off, or who had not yet come on,
is ignored because that player was not removed from the pitch. The preview also
records the sending-off minute separately, so a sent-off player on a losing
team can use the entry-to-final-whistle result share described in the rating
model. Wins and draws use the actual minutes. Minutes are clamped to the 1-120
range accepted by ElevenLedger, and an import plan whose minutes exceed the
final whistle is rejected before any club or player is created.

The imported performance is the player's final WhoScored rating. WhoScored
stores a rating history under `stats.ratings`, keyed by match minute; ElevenLedger
uses the value at the highest minute, rounded to two decimals. Unused
substitutes have no rating history and are omitted anyway. If a player has no
usable rating, or a value falls outside ElevenLedger's 1.0-10.0 performance
range, the performance defaults to 6.00. Every imported or fallback performance
can be edited before import.

Player names are split at the first space: the first token becomes the first
name and the remainder becomes the last name. A single-word name is stored as a
first name. Both fields are editable in the preview. Each name field is limited
to 47 UTF-8 bytes, so a full name can use 47 bytes for each part plus the
separating space; a source name that does not fit is rejected rather than
silently truncated.

New players start at the default rating of 50.0. Existing players keep their
stored name, starting rating, status and history.

The preview shows the same live rating summary as the Record match and Edit
match dialogs: both line-up scores, team performance averages, the expected
home result and the home team's projected change for a one-goal win or defeat.
A reused existing player uses their rating immediately before the imported
fixture's chronological position; a newly created player starts at 50.0. The
summary updates when names, clubs, minutes, performances, per-player actions or
the neutral-venue option change.

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

The cue is recomputed whenever a first name, last name or club field changes.
The row returns to the default action for its new cue unless you explicitly
chose an action for that same existing player. Explicit choices survive further
name or club edits until the row matches a different player. If the imported
club already has an active player with the same name, that player is preferred
even when the same name also exists in another club.

If correcting a name or club creates a match with a player in another club, the
row turns amber and the action button becomes available immediately. Archived
players cannot be used in a new match.

If the same existing player would be used twice in the imported line-up, the
import is rejected with a validation error.

## Source requirements and privacy

The pasted source must contain WhoScored's embedded `matchCentreData`, be valid
UTF-8, and be no larger than 16 MiB. The import fails with a clear message if the
source is only rendered page text, uses an unsupported page layout, or has no
usable date, teams, score or player minutes.

The clipboard text is read only when the user chooses **Paste page source**. It
is parsed in memory, then discarded. ElevenLedger makes no network requests and
stores nothing from the source except the confirmed match, clubs and players in
the local data file.
