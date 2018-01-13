#include "textcheats.h"
#include "cheats.h"
#include "graphics.h"
#include "objectpool.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#define TOKEN_TITLE     (1 << 0)
#define TOKEN_CHEAT     (1 << 1)
#define TOKEN_CODE      (1 << 2)
#define TOKEN_VALID     TOKEN_TITLE|TOKEN_CHEAT|TOKEN_CODE
#define TOKEN_START     TOKEN_TITLE

static cheatsGame_t *gamesHead = NULL;
static cheatsGame_t *game = NULL;
static cheatsCheat_t *cheat = NULL;

static u8 *tokens = NULL;

static void countTokens(const char *text, size_t length, int *numGames, int *numCheats, int *numCodeLines);
static int getToken(const char *line);
static int parseLine(const char *line);

cheatsGame_t* textCheatsOpen(const char *path, unsigned int *numGamesRead)
{
    FILE *txtFile;
    char *text;
    char *end;
    char *endPtr;
    char *buff;
    unsigned int lineLen;
    size_t txtLen;

    int numGames, numCheats, numCodeLines;
    
    if(!path)
        return NULL;
    if(!numGamesRead)
        return NULL;
    
    txtFile = fopen(path, "r");
    fseek(txtFile, 0, SEEK_END);
    txtLen = ftell(txtFile);
    fseek(txtFile, 0, SEEK_SET);
    
    buff = calloc(txtLen + 1, 1);
    text = buff;
    endPtr = text + txtLen;
    fread(text, 1, txtLen, txtFile);
    fclose(txtFile);
    
    tokens = malloc(512 * 1024); // 512 KB
    
    countTokens(text, txtLen, &numGames, &numCheats, &numCodeLines);
    
    while(text < endPtr)
    {
        float progress = 0.5 + (1.0 - ((endPtr - text)/(float)txtLen))/2.0;
        graphicsDrawLoadingBar(100, 375, progress);
        graphicsRenderNow();
        end = strchr(text, '\n');
        if(!end) // Reading the last line
            end = endPtr;
        
        lineLen = end - text;
        
        if(lineLen)
        {
            // remove trailing whitespace;
            char *c;
            for(c = text + lineLen; isspace(*c); --c)
                *c = '\0';

            parseLine(text);
        }
        
        text += lineLen + 1;
    }
    
    free(buff);
    free(tokens);

    *numGamesRead = numGames;
    
    return gamesHead;
}

int textCheatsSave(const char *path)
{
    return 1;
}

static void countTokens(const char *text, size_t length, int *numGames, int *numCheats, int *numCodeLines)
{
    const char *endPtr = text + length;
    const char *end;
    char line[255];
    int lineLen;
    int token;
    unsigned int tokenOffset = 0;
    if(!text || !numGames || !numCheats || !numCodeLines)
        return;

    *numGames = 0;
    *numCheats = 0;
    *numCodeLines = 0;
        
    while(text < endPtr)
    {
        float progress = (1.0 - ((endPtr - text)/(float)length))/2.0;
        graphicsDrawLoadingBar(100, 375, progress);
        graphicsRenderNow();
        end = strchr(text, '\n');
        if(!end)
            end = endPtr;
        
        lineLen = end - text;
        if(lineLen)
        {
            strncpy(line, text, 255);

            // remove trailing whitespace;
            char *c;
            for(c = line + lineLen; isspace(*c); --c)
                *c = '\0';
            
            token = getToken(line);
            tokens[tokenOffset++] = token;

            switch(token)
            {
                case TOKEN_TITLE:
                    *numGames += 1;
                    break;
                case TOKEN_CHEAT:
                    *numCheats += 1;
                    break;
                case TOKEN_CODE:
                    *numCodeLines += 1;
                    break;
                default:
                    break;
            }
        }
        text += lineLen + 1;
    }
}

// Determine token type for line.
static int getToken(const char *line)
{
    const char *c;
    int numDigits = 0, ret;
    size_t len;
    
    len = strlen(line);
    
    if (!line || len <= 0)
        return 0;
    
    if(len == 17 && line[8] == ' ')
    {
        c = line;
        while(*c)
        {
            if(isxdigit(*c++))
                numDigits++;
        }
        
        if(numDigits == 16)
            ret = TOKEN_CODE;
        else
            ret = TOKEN_CHEAT;
    }
    
    else if(line[0] == '"' && line[len-1] == '"')
        ret = TOKEN_TITLE;
        
    else if(line[0] == '/' && line[1] == '/')
        ret = 0;
    
    else
        ret = TOKEN_CHEAT;

    return ret;
}

// Parse line and process token.
static int parseLine(const char *line)
{
    cheatsGame_t *newGame;
    static unsigned int tokenOffset = 0;
    int token;
    
    token = tokens[tokenOffset++];
    
    switch(token)
    {
        case TOKEN_TITLE: // Create new game
            newGame = objectPoolAllocate(OBJECTPOOLTYPE_GAME);

            if(!game)
            {
                // First game
                gamesHead = newGame;
                game = gamesHead;
            }
            else
            {
                if(game->codeLinesUsed > 0)
                {
                    game->codeLinesCapacity = game->codeLinesUsed;
                    game->codeLines = realloc(game->codeLines, game->codeLinesCapacity * sizeof(u64));
                }
                
                game->next = newGame;
                game = newGame;
            }
            
            strncpy(game->title, line+1, 81);
            game->title[strlen(line) - 2] = '\0';
            game->next = NULL;
            break;
            
        case TOKEN_CHEAT: // Add new cheat to game
            if(!game)
                return 0;

            cheatsCheat_t *newCheat = objectPoolAllocate(OBJECTPOOLTYPE_CHEAT);

            if(game->cheats == NULL)
            {
                game->cheats = newCheat;
                cheat = game->cheats;
            }
            else
            {
                cheat->next = newCheat;
                cheat = cheat->next;
            }
            
            strncpy(cheat->title, line, 81);
            cheat->type = CHEATHEADER;
            cheat->next = NULL;
            game->numCheats++;
            break;
            
        case TOKEN_CODE: // Add code to cheat
            if(!game || !cheat)
                return 0;
            
            if(!game->codeLines)
            {
                game->codeLinesCapacity = 1;
                game->codeLines = calloc(game->codeLinesCapacity, sizeof(u64));
            }
            else if(game->codeLinesUsed == game->codeLinesCapacity)
            {
                game->codeLinesCapacity *= 2;
                game->codeLines = realloc(game->codeLines, game->codeLinesCapacity * sizeof(u64));
            }
            
            if(cheat->numCodeLines == 0)
            {
                cheat->codeLinesOffset = game->codeLinesUsed;
            }
            
            u64 *codeLine = game->codeLines + cheat->codeLinesOffset + cheat->numCodeLines;
            sscanf(line, "%08X %08X", (u32 *)codeLine, ((u32 *)codeLine + 1));
            cheat->type = CHEATNORMAL;
            cheat->numCodeLines++;
            game->codeLinesUsed++;
            break;
            
        default:
            break;
    }
    
    return 1;
}
