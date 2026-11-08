# AI Game Engine

We're building a production-grade, simple and straight-forward football player rating calculator for Windows. This is professional-grade software that needs to be stable, fully-tested, and real-world deployable. No hacks, no shortcuts - solid as a rock.

## Dev Workflow
- Ask questions only when absolutely necessary - work autonomously
- IMRORTANT! DO NOT look outside of working directory or use other code on my computer as reference
- Testing is of the utmost importance - use lots of unit tests (any framework, simple is good), run automated testing
- Do things properly, this is production-grade and not a hack project
- Create AGENTS.md and relevant skills to support development, and contain concrete development guidelines
- Use a local git repository, do not push the repository online, simply use git to manage the versioning of the project. IMPORTANT: do a code review before committing, and make sure all changes comply with production-grade quality standards, and have been properly tested/have unit tests that have passed where necessary

## Tech stack
- C as main programming language
- CMake for build management

## Basic Architecture
- Static executable with GUI (can use CLay https://www.nicbarker.com/clay for the GUI, but it is up for discussion)
- Need to have various "pages" to be able to manage data in an efficient manner

## Rating considerations
- As football is a team-based sport, the rating calculation used should account for that when calculating how a vistory/draw/loss affects the rating of individual players
- Players don't always play for the full duration of the game, this should also be taken into account in the rating calculation
