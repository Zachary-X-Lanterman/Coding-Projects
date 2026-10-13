/* Card Encoding
Each card is a 2-digit hex number (one byte)
The first digit is the rank of the card
The second digit is the suit

To get the rank, Bitshift it left four times.
to get the suit, & with bitmask 00001111

0 = Blank card
1 = Unused
2 - 9 are themseves
A = Rank 10
B = Rank Jack
C = Queen
D = King
E = Ace
F = Blank Card

0 = Spade
1 = Heart
2 = Club
3 = Diamond
4 = Blank Card

example: 0x43 would be the four of diamonds
0x00 refers to a blank card, and is ignored whenever scoreHand is called.
//TODO Actually implement lol, make 0x00 the blank card to make overhead easier
Technically, it's the zero of spades, but this isn't modded balatro, so we don't need to worry about that.


~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

each hand corresponds to a number:  

0x0000 = high card
0x1000 = pair
0x2000 = two pair
0x3000 = three of a kind
0x4000 = straight
0x5000 = flush
0x6000 = full house
0x7000 = four of a kind
0x8000 = straight/royal flush

Then, the value of each card in a hand is added to this number. This is the final score which is compared to other hands.
Finally, the value is bit shifted left four times to store the handID.
*/

//TODO: Allow big blind poster to check in preflop

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#ifdef _WIN32
    #include <windows.h>
#endif

#define SWAP8(a,b) do{ if (a > b) {uint8_t _temp = a; a = b; b = _temp;} }while(0)
#define SWAP16(a,b) do{ if (a > b) {uint16_t _temp = a; a = b; b = _temp;} }while(0)

//Player states
#define OUTOFGAME 0
#define FOLDED 1
#define PLAYING 2

//Game states
#define PREFLOP 0
#define FLOP 1
#define TURN 2
#define RIVER 3
#define SHOWDOWN 4

uint8_t gameState = PREFLOP;

struct player
{
    char name[11];
    uint8_t hand[2];
    unsigned int funds;
    uint8_t playerState;
    bool isBot;
    bool turnDone;

    //The way bots will make a choice all depends on a riskiness margin.
    // For example, the margin below is from Straight to Full House.
    //0123[456]78

    //If the bot's current hand falls within the margin, they will call.
    //If the bot's current hand falls ABOVE the margin, they will raise.
    //If the bot's current hand falls BELOW the margin, they will check if possible and fold if not.

    uint8_t riskinessBelow; 
    uint8_t riskinessAbove;  
};

struct player players[5] = {
    {"Playtester",{0x00,0x00},100,PLAYING,0},
    {"Carrie",{0x00,0x00},100,PLAYING,1,0,0,6}, //Margin is from High Card to Full House. Very risky plays.
    {"Mallok",{0x00,0x00},100,PLAYING,1,0,3,5}, //Margin is from Trips to Flush.
    {"Askii",{0x00,0x00},100,PLAYING,1,0,1,4}, //Pair to Straight
    {"ChatPCP",{0x00,0x00},100,PLAYING,1,0,0,3}, //High Card to Trips. Stupid risky plays.
};

uint8_t nextDraw;
uint8_t communityCards[5];
uint8_t deckInPlay[52];
uint16_t finalScores[5];

unsigned int pot = 0;
unsigned int minBet = 2; //Smallest possible big blind with integers.
bool canCheck = false;

uint8_t dealerButton;
uint8_t smallBlindButton;
uint8_t bigBlindButton;

uint8_t getRank(uint8_t cardIn){return cardIn >> 4;}
uint8_t getSuit(uint8_t cardIn){return cardIn & 0b00001111;}

#define MAX_DIGITS 20

//Shamefully stolen from google AI
void delay_ms(int milliseconds) {
#ifdef _WIN32
    Sleep(milliseconds); 
#else
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
#endif
}

//The following two functions have been flagrantly stolen from https://www.dev-notes.eu/2019/05/Integer-Input-in-C/
/* *
 * This function removes surplus characters from the input buffer.
 * Otherwise, if more than the permitted number of characters have been entered during the
 * call to fgets(), the surplus characters (after MAX_DIGITS chars) remain in the input buffer
 * and will be wrongly accepted as input on the next iteration of the loop.
 * */

static inline void ClearInputBuffer() 
{
	char c = 0;
	// Loop over input buffer and consume chars until buffer is empty
	while ((c = getchar()) != '\n' && c != EOF);
}

void getIntegerFromStdin(unsigned int *inputInteger)
{
	char *inputBuffer = malloc(sizeof(char) * MAX_DIGITS);
	memset(inputBuffer, 0, MAX_DIGITS);
	char *input = NULL;
	while (input == NULL) {
		// Note that fgets returns inputBuffer on success.
		// This becomes important when freeing - free either `input` or
		// `inputBuffer` to avoid an attempted double-free error.
		input = fgets(inputBuffer, MAX_DIGITS, stdin);
		
		// If fgets() receives less than MAX_DIGITS, the last char in the array is '\n'.
		// Therefore if the last char is not '\n', too many characters were entered.
		if (inputBuffer[strlen(inputBuffer) - 1] != '\n') {
			fprintf(stderr, "Woah, high roller, that number's a bit too big! Max input is %d digits.\n", MAX_DIGITS);
			ClearInputBuffer();
			input = NULL;
			continue;
		}

		// Check that the input can be intepreted as an integer
		// Convert to integer using `strtol()`
		errno = 0;
		char *endptr = NULL;
		*inputInteger = strtol(input, &endptr, 10);
		
		// If an integer was not found, endptr remains set to input
		if (input == endptr) {
			// Remove trailing newline by adding NUL at the index of the
			// terminating '\n' character. See man strcspn - this function
			// gets the length of a prefix substring.
			input[strcspn(input, "\n")] = 0;
			printf("I don't know what %s is, but you can't bet it!\n", input);
			input = NULL;
		}
		if (errno != 0) {
			fprintf(stderr, "That doesn't look like an integer to me...\n");
			input = NULL;
		}
	}
	free(inputBuffer);
}

void printCard(uint8_t cardIn, char after)
{
    if (cardIn == 0)
    {
        return;
    }

    uint8_t rank = getRank(cardIn);
    uint8_t suit = getSuit(cardIn);

    switch (rank)
    {
        case 0x0E:
            printf("A");
            break;
        case 0x0A:
            printf("10");
            break;
        case 0x0B:
            printf("J");
            break;
        case 0x0C:
            printf("Q");
            break;
        case 0x0D:
            printf("K");
            break;
        default:
            printf("%d",rank);
    }
    switch (suit)
    {
        case 0x00:
            printf("♠");
            break;
        case 0x01:
            printf("♥");
            break;
        case 0x02:
            printf("♣");
            break;
        case 0x03:
            printf("♦");
            break;
        default:
            printf("Gaster");
            break;
    }
    
    putchar(after);
}

char *handNames[] = 
{
    "High card",
    "Pair",
    "Two Pair",
    "Three of a kind",
    "Straight",
    "Flush",
    "Full House",
    "Four of a kind",
    "Straight flush",
    "Royal flush"
};

uint8_t checkForStraightFlush(uint8_t checkingHand[], uint8_t startOfStraight, uint8_t suitToFind)
{
    uint8_t i = 0;
    uint8_t matchCount = 0;
    if (startOfStraight == 100) //A-5 Detection Mode
    {
        //printf("Checking for A-5 flush...\n");
        //Here, we define an A-5 flush as A2345, and add the appropriate suit.
        uint8_t A5Flush[] = {
            0xE0 + suitToFind,
            0x20 + suitToFind,
            0x30 + suitToFind,
            0x40 + suitToFind,
            0x50 + suitToFind
        };

        for (i = 0; i < 7; i++) //scan through hand
        {
            //printf("Currently searching for %x, current card is %x...\n",A5Flush[matchCount],checkingHand[i]);
            if (matchCount == 4)
            {
                //printf("A5 FLUSH DETECTED!\n");
                return 1; //Return 1, yes! This is an A5 flush!
            }
            if (checkingHand[i] == A5Flush[matchCount]) //If the current card we're looking for has been found
            {
                //printCard(checkingHand[i],false);
                //printf(" Has been found!\n");
                i = -1; //Go back to scanning the start of the hand again. CHANGE IF -1 FUCKS SOMETHING UP!
                matchCount++; //Increase the match count, as one card matches.
                //Repeat this process untill AKQJ10 are all found.
            }
        }
        return 0;
        //If the whole hand is scanned without the match, then the for loop exits, and we return 0.
        //This signifies that it is NOT an A5 flush. 
    }

    else //Straight Flush Detection Mode
    {
        //printf("Checking for Straight Flush of %x\n",suitToFind);

        uint8_t cardToFind = (startOfStraight << 4) + suitToFind;
        //startOfStraight << 4 will put the rank in the first digit, and adding the suit will put it in the second.
        for (i = 0; i < 7; i++) //scan through hand
        {
            //printf("Currently searching for %x, current card is %x...\n",cardToFind,checkingHand[i]);
            if (matchCount == 4)
            {
                return 1; //Return 1, yes! This is a straight flush!
            }
            if (checkingHand[i] == cardToFind) //If the current card we're looking for has been found
            {
                //printCard(checkingHand[i],false);
                //printf(" has been found!\n");
                i = -1; //Go back to scanning the start of the hand again. CHANGE IF -1 FUCKS SOMETHING UP!
                cardToFind -= 16; //decrease the rank by one by subtracting 16 (remember cards are stored as hex values.)
                matchCount++; //Increase the match count, as one card matches.
                //Repeat this process until 5 matches are found.
            }
        }
        return 0;
        //If the whole hand is scanned without the match, then the for loop exits, and we return 0.
        //This signifies that it is NOT a straight flush. Alas!
    }
}

uint8_t drawNewCard() 
{
    uint8_t newCard = deckInPlay[nextDraw];
    nextDraw++;
    return newCard;
}

void resetDeck()
{
    uint8_t i = 0;
    uint8_t suit = 0;
    uint8_t rank = 2;
    //First, refill the deck with all 52 cards.
    for (i = 0; i < 52; i++)
    {
        deckInPlay[i] = (rank << 4) + suit; //Send the rank to the second digit and add the suit to the first
        rank++;
        if (getRank(deckInPlay[i]) == 0x0E) //If we reach an ace...
        {
            rank = 2; //Reset the rank back to 2 to start again,
            suit++;   //and increase the suit.
        }
    }
    //Next, fisher-yates shuffle the deck.
    for (uint8_t i = 51; i > 0; i--)
    {
        uint8_t newIndex = rand() % (i + 1);
        SWAP8(deckInPlay[i],deckInPlay[newIndex]);
    }
}
/*
void bot1Turn()
{
    //Bot1 is random.
    if (rand() % 2 == 0)
    {
        printf("Yazzabazzazzabazoo... Fold...\n");
    }
    else
    {
        printf("Yippayappayawoowa! Raise!\n");
    }
}
void bot2Turn()
{
    //Bot 2 likes even numbers. He'll raise if the community cards + his own are even and fold if not.
    uint8_t total = 
    bot2Hand[0] + 
    bot2Hand[1] +
    communityCards[0] +
    communityCards[1] +
    communityCards[2] +
    communityCards[3] +
    communityCards[4];
    if ((total & 1) == 1) //If the least significant bit is 1, it must be odd.
    {
        printf("Bot 2: Hmm. Yeah, I don't like these numbers. Fold.\n");
    } 
    else
    {
        printf("Bot 2: I have a good feeling about these numbers. Raise.\n");
    }
}
void bot3Turn()
{
    //Bot three likes the color red. He will raise if the majority of cards are red.
    uint8_t redCount = 0;
    uint8_t blackCount = 0;

    if (getSuit(bot3Hand[0]) == 2 || getSuit(bot3Hand[0]) == 0) {blackCount++;}
    if (getSuit(bot3Hand[0]) == 2 || getSuit(bot3Hand[0]) == 0) {blackCount++;}

    for (uint8_t i = 0; i < 4; i++)
    {
        if (getSuit(bot3Hand[0]) == 2 || getSuit(bot3Hand[0]) == 0) {blackCount++;} else {redCount++;}
    }
    if (redCount > blackCount)
    {
        printf("Bot 3: RED. RED LIKE BLOOD. YOUR BLOOD. RAISE.\nBot 1: Zipadawap! None of us have blood, weez beez botz!\nBot 2: Well, we're bots. The bloke behind the screen ain't.\n");
    }
    else
    {
        printf("BLACK. BLACK LIKE MIDNIGHT. NO STARS. NO RAISES. ONLY FOLDS.\n");
    }
}
*/
uint16_t scoreHand(uint8_t hand[], uint8_t handID)  
{
    uint8_t i = 0; //general iterator

    /*
    This will be the variable we return as the hand.
    The four most significant bits will be the hand type. (0 - 8)
    The next eight bits will store the actual value of the hand.
    That way, comparing who has the best hand is as simple as organizing the numbers from least to greatest.
    //the final four bits represents the hand ID --- whos hand is this? 0 = player, 1-3 = bots 1-3

    example:
    0x2391
    2 represents a two pair.
    39 is the total score.
    1 represents that this is the hand of bot 1.
    */

    uint16_t finalHand = 0;

    //Creating an array of the suits of the scoring hand and the community cards.
    uint8_t totalHand[] = {
        hand[0],
        hand[1],
        communityCards[0],
        communityCards[1],
        communityCards[2],
        communityCards[3],
        communityCards[4]
    };

    //I have to initalize it to 0 because, even though I put an initalizer,
    //C still won't zero out the array for me. Dafuq

    //We'll reuse the array of 4 for later, hence the weird name.
    //To begin, CSAOAK will count the occurences of each suit.
    /*
    [0] = Number of Spades
    [1] = Number of Hearts
    [2] = Number of Clubs
    [3] = Number of Diamonds
    */
    uint8_t countSuitsAndOfAKind[4] = {0};
    uint8_t countRanks[13] = {0};


    //First, we'll count up suits and ranks.
    for (i = 0; i < 7; i++)
    {
        if (totalHand[i] == 0) { continue; } //skip the card entirely if it's 0x00.
        else
        {
            countSuitsAndOfAKind[getSuit(totalHand[i])]++;
            //We can do this becuase the memory locations map to each suit.
            //If suit 0 is detected, it will increment memory location 0.

            countRanks[getRank(totalHand[i]) - 2]++;
            //We cannot do the same with ranks, as there is no rank 0 (hence the -1).
            //So, twos will be stored at location 0, threes at 1, fours at 2, and so on.
            //It sucks, but eh. C'set la C.
        }
    }

    //This loop will detect if there is a flush present.
    //If there is, that number will be stored in CSAOAK index [0] for later.
    for (i = 0; i < 4; i++)
    {
        if (countSuitsAndOfAKind[i] >= 5)
        {
            countSuitsAndOfAKind[0] = i;
            //printf("Flush of %d detected!\n", countSuitsAndOfAKind[0]);
            //finalHand = 0x500; //A flush is guarenteed. But we don't score it yet, as it could be a better hand...
            break;
        }
        else
        {
            countSuitsAndOfAKind[0] = 4; //TODO, optimise so this line only runs once
        }
    }

    //CSAOAK memory:
    /*
    [0] = Flush Suit (Or 4 if there is no flush)
    [1] = Number of Hearts
    [2] = Number of Clubs
    [3] = Number of Diamonds
    */

    //This next if statement will check the very special case of a straight of A2345.
    //This is because Ace is rank 14 in the memory, which won't be counted as ajacent to 2.
    //printf("Checking for A5 straight...\n");
    if (
        countRanks[12] >= 1 && //We check for an ace first, as && is a short-circuit evlauator, meaning it
        countRanks[0] >= 1 && //automatically ignores the rest of the if if there is no ace.
        countRanks[1] >= 1 &&
        countRanks[2] >= 1 &&
        countRanks[3] >= 1
    )
    {
        countSuitsAndOfAKind[1] = 100; //100 represents a straight of A-5.
        /*
        [0] = Flush Suit (Or 4 if there is no flush)
        [1] = 100, representing an A-5 straight.
        [2] = Number of Clubs
        [3] = Number of Diamonds
        */
       //If a flush is present and a straight flush is detected...
        if (countSuitsAndOfAKind[0] != 4 && checkForStraightFlush(totalHand,100,countSuitsAndOfAKind[0]) == 1)
        {
            //0x800F corresponds to a straight flush + 1 + 2 + 3 + 4 + 5. We can just return it and be done.
            return (0x80F0 + handID);
        }
    }

    //If we already know it's an A-5 straight, we don't need to bother with the rest of this straight checking routine.
    //Skip it only if CSAOAK[1] says that there's an A-5 straight.
    //If not, we gotta count non A-5 straights.
    if (countSuitsAndOfAKind[1] != 100)
    {
        countSuitsAndOfAKind[1] = 0; //Zeroing the value before checking for a straight so that it stays 0 if there isn't a straight.

        //Now it is time to detect a straight. We start with the highest ranking cards and count down.
        //If there are 5 cards in a series, it's a straight. By starting at the end, we naturally filter out the lower ranks.
        //For example, if there's a straight of 10,9,8,7,6,5,4, then by starting at 10 and stopping at 6 we can ignore the 5 and 4.
        for (i = 12; i > 3; i--)
        {
            //This is a bit of a clunky way to do it, but it's checking every value and the four values less than it.
            //If they're all greater then one, it's a straight!

            if (
                countRanks[i] > 0 &&
                countRanks[i - 1] > 0 &&
                countRanks[i - 2] > 0 &&
                countRanks[i - 3] > 0 &&
                countRanks[i - 4] > 0
            )
            {
                //printf("Straight detected starting at %d and ending at %d.\n", i + 2, i - 2);
                //First, assign CSAOAK[1] to the card (not the memory location) that the straight starts at.
                countSuitsAndOfAKind[1] = i + 2;
                //First, check to see if it's a royal flush. If the straight starts on Ace and there's a flush, it's gotta be!
                if (i + 2 == 14 && checkForStraightFlush(totalHand,14,countSuitsAndOfAKind[0]) == 1)
                {
                    return (0x93C0 + handID);
                }//If it's not a royal flush, check if it's a royal straight.
                else if (checkForStraightFlush(totalHand,countSuitsAndOfAKind[1],countSuitsAndOfAKind[0]) == 1)
                {
                    finalHand = 0x800;
                    goto readyToScore;
                }

                break;
                //CSAOAK memory:
                /*
                [0] = Flush Suit (Or 4 if there is no flush)
                [1] = The card that the straight starts at (or 0 if there is no straight or 100 if A-5)
                [2] = Number of Clubs
                [3] = Number of Diamonds
                */
            }
        }
    }

    //Now we can count pairs and threes of a kind.
    countSuitsAndOfAKind[2] = 0; //zero these out so it doesn't fuck with anything
    countSuitsAndOfAKind[3] = 0; 
    for (i = 0; i < 13; i++)
    {
        switch(countRanks[i])
        {
            case 2:
                countSuitsAndOfAKind[2]++; //CSAOAK[2] will store the number of pairs.
                break;
            case 3:
                countSuitsAndOfAKind[3]++; //CSAOAK[3] will store the number of threes of a kind.
                break;
            case 4: //If there are 4 of any card, it's gotta be a four of a kind.
                finalHand = 0x700;
                goto readyToScore;
        }
    }

    //CSAOAK memory:
    /*
    [0] = Flush Suit (Or 4 if there is no flush)
    [1] = The card that the straight starts at (or 0 if there is no straight)
    [2] = Number of pairs
    [3] = Number of threes of a kind

    each hand corresponds to a number, and we've filtered out the top three. Now we'll deal with the rest of these guys.
    0 = high card
    1 = pair
    2 = two pair
    3 = three of a kind
    4 = straight
    5 = flush
    6 = full house
    */
   
    //This block sorts out Full House, Straight, and Three of a kind.
    if (countSuitsAndOfAKind[3] >= 1) //If there's at least one three of a kind...
    {
        if (countSuitsAndOfAKind[2] >= 1) //and there's at least a pair...
        {
            finalHand = 0x600; //then there must be a full house. Because Full House trumps all remaining hands, 
            goto readyToScore; //we can score with 6 and be done.
        }
        else if (countSuitsAndOfAKind[0] == 4) //and there is NOT a flush OR a pair...
        {
            if (countSuitsAndOfAKind[1] != 0) //but there IS a straight...
            {
                finalHand = 0x400; //then there must be a straight. Becuase straight trumps three of a kind and this logic
                goto readyToScore;  //comes after checking for full house, we can score with 4 and be done
            }
            else //and there is NOT a straight...
            {
                finalHand = 0x300; //then there must be a three of a kind. Becuase straight trumps three of a kind and this logic
                goto readyToScore;  //comes after checking for full house, we can score with 4 and be done
            }
        }
    }

    /*Quick if statement for flush detection. We put it here as all the hands better than flush have been ruled out,
    but all hands worse than flush have yet to be scored. Because CSAOAK[0] was set to a flush value 
    earlier, we can set totalHand to 0x500 if CSAOAK is not 4. */
    if (countSuitsAndOfAKind[0] != 4)
    {
        finalHand = 0x500;
        goto readyToScore;
    }


    
    //This block sorts out Two Pair, Pair, and High Card.
    if (countSuitsAndOfAKind[1] > 0) //If there's a straight as indicated by CSAOAK[1], return straight.
    {
        finalHand = 0x400;
    }
    else if (countSuitsAndOfAKind[2] >= 2) //If there are two or three pairs, then the best hand possible is two pair.
    {
        finalHand = 0x200;
    }
    else if (countSuitsAndOfAKind[2] == 1) //Pair
    {
        finalHand = 0x100;
    }
    //a measley high card will require no extra logic, as finalHand is initalized as 0.
    //a flush also requires no extra logic, as if a flush is detected, finalHand is set to 5.
    
    readyToScore://Now that we've identified what hand type it is, we gotta score it!

    switch (finalHand)
    {
        case 0x800:
            for (i = 0; i < 5; i++)//For any other straight flush, add the value at CSAOAK[1] 5 times, decrementing each time.
            {
                finalHand += countSuitsAndOfAKind[1];
                countSuitsAndOfAKind[1]--;
            }
            break;
        case 0x700: //For a 4-of-a-kind, find the location at which countRanks == 4.
            for (i = 12; i >= 0; i--)  //Similar to straight detection, we'll start at the end.
            {
                if (countRanks[i] == 4) //If a count of four is detected,
                { //add the card value (expressed as index + 2) multiplied by four.
                    finalHand += ((i + 2) << 2);
                    break; //break out of the loop.
                }
            }
            break; //break out of the case. I wish C had two different breaks --- one for loops and one for switches.
        case 0x600: //for a full house, find the highest pair and highest three of a kind.
            //Here, CSAOAK[0] will act as what we're searching for (pairs or triples).
            countSuitsAndOfAKind[0] = 3;
            for (i = 12; i >= 0; i--)  //Similar to straight detection, we'll start at the end.
            {
                if (countSuitsAndOfAKind[0] == 1) //If CSAOAK[0] == 1, thenthat means we've detected our pair and triple.
                {
                    break; //We're done, get outta there!
                }
                if (countRanks[i] == countSuitsAndOfAKind[0]) //If a count of two or three is detected,
                { //add the card value multiplied by two or three.
                    finalHand += ((i + 2) * countSuitsAndOfAKind[0]);
                    i = 12; //go back to the start
                    countSuitsAndOfAKind[0]--; 
                    /*and decrement CSAOAK[0]. If the loop is acting with CSAOAK[0] == 3, then that means
                    we've detected a triple. Decrementing it makes the loop looks for doubles instead.
                    */
                }
            }
            break;
        case 0x500: //Flush is unique, as it's one of the two cases that requires use of totalHand!
            //Because we need the hand sorted for this, we'll use a 7-input sorting network:
            //Stolen from https://bertdobbelaere.github.io/sorting_networks.html#N7L16D6
            SWAP8(totalHand[0],totalHand[6]); SWAP8(totalHand[2],totalHand[3]); SWAP8(totalHand[4],totalHand[5]);
            SWAP8(totalHand[0],totalHand[2]); SWAP8(totalHand[1],totalHand[4]); SWAP8(totalHand[3],totalHand[6]);
            SWAP8(totalHand[0],totalHand[1]); SWAP8(totalHand[2],totalHand[5]); SWAP8(totalHand[3],totalHand[4]);
            SWAP8(totalHand[1],totalHand[2]); SWAP8(totalHand[4],totalHand[6]);
            SWAP8(totalHand[2],totalHand[3]); SWAP8(totalHand[4],totalHand[5]);
            SWAP8(totalHand[1],totalHand[2]); SWAP8(totalHand[3],totalHand[4]); SWAP8(totalHand[5],totalHand[6]);
            /*Now that the hand is sorted by rank, we can simply go through the hand and add the value
            of any card that matches CSAOAK[0]*/
            for (i = 6; i > 1; i--)
            {
                if (getSuit(totalHand[i]) == countSuitsAndOfAKind[0])
                {
                    finalHand += getRank(totalHand[i]);
                }
            }
            break;
        case 0x400: //Scoring a straight is exactly the same as scoring a straight flush.
            if (countSuitsAndOfAKind[1] == 100) //However, we have to double check for an A5 flush first!
            {
                return (0x40F0 + handID);
            }
            else
            {
                for (i = 0; i < 5; i++)
                {
                    finalHand += countSuitsAndOfAKind[1];
                    countSuitsAndOfAKind[1]--;
                }
            }
            break;
        case 0x300: //Three of a kind
            for (i = 12; i >= 0; i--)  //Similar to straight detection, we'll start at the end.
            {
                if (countRanks[i] == 3) //If a count of three is detected,
                { //add the card value multiplied by  three.
                    finalHand += ((i + 2) * 3);
                    break;
                }
            }
            break;
        case 0x200: //For two pair, find the highest two pairs.
            //Here, CSAOAK[0] will count how many pairs there are left to find.
            countSuitsAndOfAKind[0] = 2;
            for (i = 12; i >= 0; i--)  //Similar to straight detection, we'll start at the end.
            {
                if (countSuitsAndOfAKind[0] == 0) //If CSAOAK[0] == 0, thenthat means we've detected our two high pairs.
                {
                    break; //We're done, get outta there!
                }
                if (countRanks[i] == 2) //If a count of two is detected,
                { //add the card value multiplied by two.
                    finalHand += ((i + 2) << 1);
                    i = 12; //go back to the start
                    countSuitsAndOfAKind[0]--; 
                    /*and decrement CSAOAK[0]. If the loop is acting with CSAOAK[0] == 1, then that means
                    we've detected our first doubble. Decrementing it makes the loop look for the second double.
                    */
                }
            }
            break;
        case 0x100: //Pair. Same as three of a kind, but we detect pairs instread of threes of a kind.
            for (i = 12; i >= 0; i--)  //Similar to straight detection, we'll start at the end.
            {
                if (countRanks[i] == 2) //If a count of 2 is detected,
                { //add the card value multiplied by  2.
                    finalHand += ((i + 2) << 1);
                    break;
                }
            }
            break;
        case 0: //High card. Funnily enough, the simplest hand requires the sorting network.
            SWAP8(totalHand[0],totalHand[6]); SWAP8(totalHand[2],totalHand[3]); SWAP8(totalHand[4],totalHand[5]);
            SWAP8(totalHand[0],totalHand[2]); SWAP8(totalHand[1],totalHand[4]); SWAP8(totalHand[3],totalHand[6]);
            SWAP8(totalHand[0],totalHand[1]); SWAP8(totalHand[2],totalHand[5]); SWAP8(totalHand[3],totalHand[4]);
            SWAP8(totalHand[1],totalHand[2]); SWAP8(totalHand[4],totalHand[6]);
            SWAP8(totalHand[2],totalHand[3]); SWAP8(totalHand[4],totalHand[5]);
            SWAP8(totalHand[1],totalHand[2]); SWAP8(totalHand[3],totalHand[4]); SWAP8(totalHand[5],totalHand[6]);
            //Now that the hand's in order, just get the card in the highest position.
            finalHand += getRank(totalHand[6]);

            break;
        default:
            printf("Something went wrong.\n");
    }
    //Finally, bitshift finalHand left by 4 to add on the handID, and we're done!
    return ((finalHand << 4) + handID);
}

//Moves a player can do
static inline void actionRaise(struct player *Player, unsigned int betAmount)
{
    Player->funds -= betAmount;
    pot += betAmount;
    minBet = betAmount;
    printf("%s raises, betting %d. The pot is now worth $%d.\n",Player->name,betAmount,pot);
}
 
static inline void actionCall(struct player *Player)
{
    Player->funds -= minBet;
    pot += minBet;
    printf("%s calls, adding $%d to the pot. The pot is now worth $%d.\n",Player->name,minBet,pot);
}

static inline void actionFold(struct player *Player)
{
    Player->playerState = FOLDED;
    printf("%s folds, tossing their cards onto the table.\n",Player->name);
}

static inline void actionCheck(struct player *Player)
{
    printf("%s checks, tapping their fingers on the table.\n",Player->name);
    canCheck = false;
}

static inline void playerAI(struct player *bot, uint8_t playerID)
{ //I could probably optomize this to not run scoreHand() if funds < pot, but eh. This is easier to code and follow.
    uint8_t handType = (scoreHand(bot->hand, playerID)) >> 0x0C; //Score the hand and get the type.

    if (handType < bot->riskinessBelow)
    {
        if (canCheck == 1) {actionCheck(bot);} //Note that we don't put &bot here, as bot is already a pointer.
        else {actionFold(bot);}
    }

    else if (handType > bot->riskinessAbove)
    {
        signed int whatToBet = ((bot->funds - pot) / (bot->riskinessBelow));

        if (whatToBet < 0) {actionFold(bot);}
        else if (whatToBet > 0) {actionRaise(bot, whatToBet);}
        else {actionCheck(bot);}
    }
    else {actionCheck(bot);}
}

void playerTurn()
{
    int humanPlayerInput = 0;
    bool humanPlayerTurnUnfinished = true;

    printf("Your turn, %10s!\n(F)old --- (C)all --- (R)aise",players[0].name);
    if (canCheck)
    {
        printf(" --- C(h)eck\n");
    }
    else
    {
        putchar('\n');
    }

    while (humanPlayerTurnUnfinished)
    {
        humanPlayerInput = getchar();
        ClearInputBuffer();
        switch (toupper(humanPlayerInput))
        {
            case 'R':
                if (players[0].funds < minBet)
                {
                    printf("You don't have enough funds to match the minimum bet!\n");
                    break;
                }
                unsigned int bet;
                printf("How much will you bet?\nThe minimum bet is $%d.\nThe pot currently contains $%d.\n",minBet,pot);
                do 
                {
                    getIntegerFromStdin(&bet);
                    if (bet < minBet)
                    {
                        printf("That is below the minimum bet returement!\n");
                    }
                    else if (bet > players[0].funds)
                    {
                        printf("You do not have enough money to bet that amount!\n");
                    }
                }
                while (bet < minBet);
                pot += bet;
                players[0].funds -= bet;
                printf("You have bet $%d. The pot is now worth $%d.\n",bet,pot);
                humanPlayerTurnUnfinished = false;
                break;
            case 'F':
                printf("You fold, tossing your cards onto the table face down.\n");
                players[0].playerState = FOLDED;
                humanPlayerTurnUnfinished = false;
                break;
            case 'C':
                pot += minBet;
                players[0].funds -= minBet;
                printf("You call, adding $%d to the pot. The pot is now worth $%d.\n",minBet,pot);
                humanPlayerTurnUnfinished = false;
                break;
            case 'H':
                if (canCheck)
                {
                    printf("You check, tapping two fingers against the table.\n");
                    canCheck = false;
                    humanPlayerTurnUnfinished = false;
                    break;
                }
                else
                {
                    printf("Checking is not an option right now!");
                    break;
                }
            default:
                printf("Invalid option, try again!\n");
                break;
        }
    }
    return;
}

int main()
{ 
    printf("Let the games begin!\n\n");

    srand(time(NULL));
    resetDeck();

    //Note that mod 5 will always select a number between 0 and 4. This is handy due to the cyclical nature of 
    //Big Blind <- Small Blind <- Dealer. players[dealerbutton + 1] posts the small blind and + 2 posts the big blind.

    dealerButton = rand() % 5; //Select a random player to have the dealer button.
    smallBlindButton = (dealerButton + 1) % 5;
    bigBlindButton = (dealerButton + 2) % 5;

    //First, post the small and big blinds based on who's right after the dealer.
    printf("%s is now the dealer!\n",players[dealerButton].name);

    if (players[(smallBlindButton) % 5].funds < (minBet >> 1))
    {
        printf("%s cannot match the small blind! %s is BUST!\n",players[(smallBlindButton) % 5].name,players[(smallBlindButton) % 5].name);
        players[(smallBlindButton) % 5].playerState = OUTOFGAME;
    }
    else
    {
        printf("%s posts the small blind of $%d.\n",players[(smallBlindButton) % 5].name,(minBet >> 1));
        players[(smallBlindButton) % 5].funds -= (minBet >> 1);
        pot += (minBet >> 1);
    }
    
    //repeat for big blind
    if (players[(bigBlindButton) % 5].funds < (minBet))
    {
        printf("%s cannot match the big blind! %s is BUST!\n\n",players[(bigBlindButton) % 5].name,players[(bigBlindButton) % 5].name);
        players[(bigBlindButton) % 5].playerState = OUTOFGAME;
    }
    else
    {
        printf("%s posts the big blind of $%d.\n\n",players[(bigBlindButton) % 5].name,(minBet));
        players[(bigBlindButton) % 5].funds -= (minBet);
        pot += (minBet);
    }

    //Deal every player in.
    //TODO: Make dealing start with small blind poster.
    for (uint8_t i = 0; i < (sizeof(players) / sizeof(players[0])); i++)
    {
        players[i].hand[0] = drawNewCard();
    }
    for (uint8_t i = 0; i < (sizeof(players) / sizeof(players[0])); i++)
    {
        players[i].hand[1] = drawNewCard();
    }

    //now, go through the of the players and have them do their turn, starting with the player Under The Gun.
    uint8_t currentPlayer = (bigBlindButton + 1) % 5;

    //Main game loop. Behaviour changes depending on gameState.
    while (gameState != SHOWDOWN)
    {
        //This inner while loop runs once for every round of betting. It continues until every player has gone,
        //as evidenced by currentPlayer.turnDone == true. If so, we must have gone through the full loop,
        //as there's no other reason why it would be true.
        while(players[currentPlayer].turnDone == false)
        {
            if (players[currentPlayer].isBot == false)
            {
                printf("Your hand: ");
                printCard(players[0].hand[0],' ');
                printCard(players[0].hand[1],'\n');

                playerTurn();
            }
            else
            {
                playerAI(&(players[currentPlayer]), currentPlayer); ///NMIGHT BE WRONG>!>!??!?!?
            }
            //Lastly, mark the current player as finished with their turn and move on to the next.
            players[currentPlayer].turnDone = true;
            currentPlayer = (currentPlayer + 1) % 5;
        }
        //At the end of each round of betting...
        //First, reset all necessary values to their defualt states for the next round.
        for (uint8_t i = 0; i < 5; i++) 
        {
            players[i].turnDone = false;
        }
        currentPlayer = smallBlindButton;
        canCheck = true;
        
        //Next, advance the game to the next round, and deal new cards accordingly.
        gameState++;
        switch (gameState)
        {
        case FLOP:
            nextDraw++; //burn a card.
            printf("The dealer burns a card and adds ");
            for (uint8_t i = 0; i < 3; i++)
            {
                communityCards[i] = drawNewCard();
                printCard(communityCards[i],' ');
            }
            printf("to the table.\n");
            break;
        case TURN:
            nextDraw++;
            printf("The dealer burns a card and adds ");
            communityCards[3] = drawNewCard();
            printCard(communityCards[3],' ');
            printf("to the table.\n");
            break;
        case RIVER:
            nextDraw++;
            printf("The dealer burns a card and adds ");
            communityCards[4] = drawNewCard();
            printCard(communityCards[4],' ');
            printf("to the table.\n");
            break;
        default:
            break;
        }
    }   

    printf("The time has come... FOR THE SHOWDOWN!\nThe community cards:\n");
    for (uint8_t i = 0; i < 5; i++)
    {
        printCard(communityCards[i],' ');
    }
    putchar('\n');
    for (uint8_t i = 0; i < 5; i++)
    {
        finalScores[i] = scoreHand(players[i].hand, i);
        printf("%s has ",players[i].name);
        printCard(players[i].hand[0], ' ');
        printCard(players[i].hand[1], '\n');
        printf("A %s!\n", handNames[finalScores[i] && 0xF]); //Bitwising with 15 gets the last hex digit
    }

    //Using a 4 input sorting network to order the final scores from least to greatest.
    SWAP16(finalScores[0],finalScores[2]); SWAP16(finalScores[1],finalScores[3]);
    SWAP16(finalScores[0],finalScores[1]); SWAP16(finalScores[2],finalScores[3]);
    SWAP16(finalScores[1],finalScores[2]);

    printf("%s wins this hand!", players[(finalScores[0] & 15)].name);
}

/* It's time for the SHOWDOWN!

    printf("Your hand:\n");
    printCard(playerHand[0],false);
    putchar(' ');
    printCard(playerHand[1],true);

    printf("\nB1 hand:\n");
    printCard(bot1Hand[0],false);
    putchar(' ');
    printCard(bot1Hand[1],true);

    printf("\nB2 hand:\n");
    printCard(bot2Hand[0],false);
    putchar(' ');
    printCard(bot2Hand[1],true);

    printf("\nB3 hand:\n");
    printCard(bot3Hand[0],false);
    putchar(' ');
    printCard(bot3Hand[1],true);
    
    printf("\ncommunity cards:\n");

    for (uint8_t i = 0; i < 5; i++)
    {
        printCard(communityCards[i],false);
        putchar(' ');
    }
    
    finalScores[0] = scorehand(playerHand,0);
    finalScores[1] = scorehand(bot1Hand,1);
    finalScores[2] = scorehand(bot2Hand,2);
    finalScores[3] = scorehand(bot3Hand,3);

    //Using a 4 input sorting network to order the final scores from least to greatest.
    SWAP16(finalScores[0],finalScores[2]); SWAP16(finalScores[1],finalScores[3]);
    SWAP16(finalScores[0],finalScores[1]); SWAP16(finalScores[2],finalScores[3]);
    SWAP16(finalScores[1],finalScores[2]);

    putchar('\n');
    for (uint8_t i = 4; i > 0; i--)
    {
        printf("Player %d has a %s!\n", ((finalScores[i - 1] & 15) + 1),handNames[finalScores[i - 1] << 12]); //Bitwising with 15 gets the last four bits.
    }*/