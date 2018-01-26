# Tinfoil Potatoes
#### Mansour Elsharawy, Shakil Rafi, Henry Zheng<br>Systems pd4<br>Project2 -- The Final Frontier

In-terminal implementation of the Exploding Kittens card game over a network. The objective of the game is to avoid drawing an “exploding kitten” card from the deck, which “kills” the player, thus rendering them out of the game. Players can use cards such as “skip” or “draw from bottom” to avoid drawing an “exploding kitten” card.

For the full set of rules and mechanics, visit the following link: [FULL RULES](https://www.fgbradleys.com/rules/rules2/ExplodingKittens-rules.pdf) 

## Notes and Modifications
* NOPE cards have been replaced with REVERSE cards
* There is no five distinct cards feature
* Do not attempt to give unsolicited user input. Doing so will break the game. Badly.
* Do not attempt to play less than three players or more than six players.
* There may be some unknown bugs lurking...
* Some logistical information that would be known in an in-person environment are not included.

## File Structure
```
cards.c
cards.h
client.c
DESIGN
DEVLOG
forking_server.c
log.sh
makefile
networking.c
networking.h
README.md
```

## Launch Instructions
    
1. Have all players enter their terminal and go into the directory that they want to have this game in
2. Enter this command to clone our repo
```
git clone https://github.com/srafi1/tinfoil-potatoes.git
```
3. Go into the tinfoil-potatoes folder using this command
```
cd tinfoil-potatoes/
```
5. Make/compile the program
```
make
```
6. Run the server
```
./server
```
7. Have at least three but no more than six players run the client
```
./client
```
8. Enjoy the game!

