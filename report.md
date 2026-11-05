Link to the 10 minute video on this project.
https://youtu.be/XMSGH350GNg

Week 2 Proposal:
Peggle Basic is a recreation of the game Peggle, specifically Peggle Deluxe, with the more complicated aspects stripped away.
Peggle is a casual puzzle video game (genre) inspired by pachinko. Balls are shot from top down towards Pegs of varying colours, Blue, Orange (worth 10x of blue pegs), Purple (worth 50x the points of blue pegs and only 1 in play per turn that moves and respawn on different blue pegs) and Green (These won’t be included in the project, probably). There are usually 100 pegs per stage 25 orange 72 blue, 2 green and 1 purple. 
The core gameplay loop is firing balls at pegs, which upon being struck are cleared. The goal is to clear all the orange pegs in the stage without running out of balls. Balls can be regained in multiple ways; reaching a score threshold in a single shot, completely missing a shot gives a 50% chance to return the ball, and landing a ball in the bucket that moves left and right at the bottom of the screen.
For each orange peg cleared the multiplier will increase, going from 1x, 2x, 3x, 5x and 10x (when 3 orange pegs remain). Free balls are earned at a freshold of 25000, 75000 and 125000 (will be lower for this to work since some core mechanics are removed i.e. style shots). Blue pegs are worth 100 points, Orange 1000 and Purple 5000.
In this project I intend to use AI to assist me in coding and understanding the code.
The roadmap should be, make shooting ball mechanic, make the pegs, make the pegs be deleted when collided with, make the pegs different colours and give different score values when cleared, make the ball numbered mechanic and lose/win screen/popup, make moving bucket at the bottom of the screen, make the return ball mechanic (i.e. hitting bucket, 50/50 missed shot and score threshold reached in 1 shot), make a purple peg that changes every shot. Etc.

There where no deviations from the original proposal only additions.
i.e. pegpop+balldrop+reload animations, challenge modes, leader boards, bucket speed-up, slowmo+zoom on final shot, Added style shots to make the mechanic of free balls to work properly (wouldn't be enough points otherwise).
These enhance the gameplay and add to the player experience.

Dependencies
  - SDL3 and SDL3_mixer.

AI Prompts.pdf attached to repo
Use of AI
  - AI was heavily used for code creation, but not for the creative control. All the prompts were crafted to do specifically what was wanted. AI Prompts.pdf will be attached to the repository to see all of the prompts used.
AIs used
  - Claude.ai and AIStudio.google.com.
Example Prompts - Show the current high score for the level on the win screen and if you've beaten the high score instead say NEW HIGHSCORE | Add a popping animation when the pegs are deleted | I need you to fix the left side shots. you start with 9 on the left and 1 on the cannon. but after you shoot 1 gets added to the left and when the turn ends 2 disappear. What I want is for 10 to be on the left at the start then the top 1 gets shot up each turn and sits on the cannon. So there should be 1 on the cannon and 9 left to be shot in coming turns. After missing one shot and not regaining it there are 8 shots instead of 9 so you're losing double the ammount of shots per turn

Game programming patterns used
  - Game Loop (Nstrom, chapter 3)
  - State (Nstrom, chapter 7)
  - Command Pattern (Nstrom, chapter 2)
  - Event Queue (Nstrom, chapter 15)
  - Flyweight (Nstrom, chapter 4)
  - Object Pool (Nstrom, chapter 19)
  - RAII Wrapper Pattern (cleans up SDL)


Game mechanics and how they are coded
  - Aim guide (GetAimDirection)
  - Collisions (Reflects shot with some loss in speed)
  - Moving Bucket (Dwell to make it stationary at a wall and Ramp to speedup)
  - Scoring System (sets a peg.hit to true when hit then pushes it to an index m_hitorder, then mults that by FeverMultiplier then does the other calculations)
  - Style Shots (Many structs with things that track distance traveled by shot if it hit bounced of a wall, and tracking the bucket catch streak)
  - Anti-stick (deletes peg if shot comes to rest on it)
  - Slow-mo + zoom
  - 50/50 Roulette
  - Challenge mode (state persistence)
  - Leaderboard with encryption

<img width="801" height="624" alt="Screenshot 2026-11-05 140511" src="https://github.com/user-attachments/assets/2a4c868e-fd2b-46d3-9478-a4126a7ca7d2" />
<img width="790" height="626" alt="Screenshot 2026-11-04 210038" src="https://github.com/user-attachments/assets/afc4f196-2e7c-4704-a469-aa9065eac6a1" />

Playtesting.pdf is attached to repo
Mainly about implementing the ideas brought up during the sessions and a bug fix.

If anything I would've liked to try to do green pegs/hero abilities.
