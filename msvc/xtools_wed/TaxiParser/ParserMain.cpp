#include "ParserMain.h"
#define _CRT_SECURE_NO_WARNINGS

ParserMain::ParserMain(void)
{
}


ParserMain::~ParserMain(void)
{
}

//Return if there was an error or not
static bool ValidateBasics(InString * inStr)
{
	bool error = false;

	//---White Space-----------------------------------------
	printf("Checking to see if there is any whitespace...\n");
	while(inStr->nPos != inStr->endPos)//Loop for the whole string
	{
		//If the current charecter is white space
		if(isspace(*inStr->nPos))
		{
			printf("Char %d is whitespace.\n", inStr->count);
			return true;
		}
		//Increase the pointer and counter
		inStr->nPos++;
		inStr->count++;
	}
	printf("No whitespace errors detected. Moving on to ASCII Checking...\n");
	//-------------------------------------------------------

	//Reset variable
	inStr->ResetNPos();

	//--ASCII------------------------------------------------
	while(inStr->nPos != inStr->endPos)
	{	
		if( ((int) *inStr->nPos < 33 ) || ((int) *inStr->nPos > 126))
		{
			printf("Char at location %d is not valid ASCII. \n", inStr->nPos, inStr->count);
			return true;
		}
		inStr->nPos++;
		inStr->count++;
	}
	if(error == false)
	{
		printf("All ASCII valid\n");
	}
	//-------------------------------------------------------
	inStr->ResetNPos();
	
	const char * curStart = inStr->nPos;
	//end of the } brace pair, starts as the end of the string for safety
	const char * curEnd = inStr->endPos;
		
	//What is currently considered good, a { a }
	//We start by saying that we are looking for a {
	char LFGoodMode = '{';//Used later for nesting nesting

	//--Validate some basic curly brace rules
	//While there are still
	//while(true)
	//{
	//	//Find the next
	//	while(*curStart != '{')
	//	{
	//		curStart++;
	//	}
		//1.) Must have atleast 1 pair
		//2.) All { have a }
		//3.) No pair may be empyt nest
		//4.) No pair may nest

		//--Find if and where the end of the pair is-----
		//Run until it breaks on 1.)Finding }
		//or reaching the end of the string
		while(true)
		{
			//If you've found the other pair
			if(*inStr->nPos == '}')
			{
				curEnd=inStr->nPos;
				break;
			}
			if(inStr->nPos == inStr->endPos)
			{
				printf("You have no end to this pair starting from %d",inStr->count);
				return true;
			}
			inStr->nPos++;
		}
		//---------------------------------------------

		//Now that you are at done finding the bounds, reset
		inStr->ResetNPos();

		//--Next, find if it is actually empty---------
		if(*(inStr->nPos+1) == '}')
		{
			printf("Empty curly braces detected!\n");
			return true;
		}
		//---------------------------------------------
			
		//--Finally see if there is nesting------------

		while(inStr->nPos != curEnd)
		{
			/* 1.)Decide whats good or bad
			*		The first curly brace should be open
			*		The second curly brace should be close
			*		It should change never
			* 2.)Test to see if it good or bad
			*/

			//If we are at a { or }
			if((int)*inStr->nPos == '{' || (int) *inStr->nPos == '}')
			{
				//Is it the good mode?
				if((int)*inStr->nPos == LFGoodMode)
				{
					//If so toggle what you are looking for
					LFGoodMode = (*inStr->nPos == '{') ? '}' : '{';					
				}
				else
				{
					error = true;
					printf("Char %c at location %d is invalid \n", *inStr->nPos,inStr->count);
				}
			}
			inStr->count++;
			inStr->nPos++;
		}
		//---------------------------------------------
		inStr->ResetNPos();
	//}
	return error;
}
static char * EnumToString(FSM in)
{
	switch(in)
	{
	case I_COMMA:
		return "I_COMMA";
	case I_INCUR:
		return "I_INCUR";
	case I_ACCUM_GLPHYS:	
		return "I_ACCUM_GLPHYS";
	case I_ANY_CONTROL:
		return "I_ANYCONTROL";
	case I_WAITING_SEPERATOR:	
		return "I_WAITING_SEPERATOR";
	case O_ACCUM_GLYPHS:
		return "O_ACCUM_GLYPHS";
	case O_END:	
		return "O_END";
	}
	return "NOT REAL STATE";
}
//Take in the current (and soon to be past) state  and the current letter being processed
static FSM LookUpTable(FSM curState, char curChar, OutString * str)
{
	printf("%c",curChar);

	//If you have reached a \0 FOR ANY REASON exit now
	if(curChar == '\0')
	{
		//FireAction(
		return O_END;
	}
 	switch(curState)
	{
	case I_COMMA:
		switch(curChar)
		{
		//You will always enter IDLE from a place where a seperator is
		//not allowed
		case '}':
		case ','://since comma's always go into idle you have hit ,,
			//FireAction(throw error)
			return LOOKUP_ERR;
		case '@':
			return I_ANY_CONTROL;
		default:
			//otherwise accumulate the glyphs
			str->AccumBuffer(curChar);
			return I_ACCUM_GLPHYS;
		}
		break;
	case I_INCUR:
		switch(curChar)
		{
		case '@':
			//FireAction(
			return I_ANY_CONTROL;
		default:
			//otherwise accumulate the glyphs
			str->AccumBuffer(curChar);
			return I_ACCUM_GLPHYS;
		}
		break;
	case I_ACCUM_GLPHYS:	
		switch(curChar)
		{
		//Cases to make it stop accumulating
		case '}':
			str->AppendLetter(str->curlyBuf,str->curBufCNT);
			str->ClearBuf();
			return O_ACCUM_GLYPHS;
		case ',':
			str->AppendLetter(str->curlyBuf,str->curBufCNT);
			str->ClearBuf();
			return I_COMMA;
		default:
			//otherwise accumulate the glyphs
			str->AccumBuffer(curChar);
			return I_ACCUM_GLPHYS;
		}
		break;
	case I_ANY_CONTROL:	
		switch(curChar)
		{
		case 'Y':
		case 'L':
		case 'R':
		case 'B':
			
			//Do action, change color
			str->curColor = curChar;
			return I_WAITING_SEPERATOR;
			
		case '@':
			str->SwitchFrontBack();
			return I_WAITING_SEPERATOR;
		}
		break;
	case I_WAITING_SEPERATOR:	
		switch(curChar)
		{
		case ',':
			return I_COMMA;
		case '}':
			return O_ACCUM_GLYPHS;
		default:
			printf("\nWas expecting , or }, got %c",curChar);//No way you should end up with something like @YX or {@@X
			return LOOKUP_ERR;
		}
		break;
	case O_ACCUM_GLYPHS:	
		switch(curChar)
		{
		case '{':
			return I_INCUR;
			break;
		default:
			str->AppendLetter(&curChar,1);
			return O_ACCUM_GLYPHS;
			break;
		}
		break;
	case O_END:	
		switch(curChar)
		{
			
		}
		break;
	}
	return LOOKUP_ERR;
}

int main(int argc, const char* argv[])
{
	printf("Welcome to the Taxi Sign Parser. \nPlease input the string now \n");
	
	//Take input and create and input string from it
	char buf[1024];
	scanf("%[^\n]",buf);
	InString inStr(buf);

	//Make the front and back outStrings
	OutString outStr;

	//Validate if there is any whitesapce or non printable ASCII charecters (33-126)
	if(ValidateBasics(&inStr) == true)
	{
		printf("\nString not basically valid \n");
		system("pause");
		return 0;
	}
	system("pause");
	system("cls");
	
	FSM FSM_MODE = O_ACCUM_GLYPHS;
	while(FSM_MODE != O_END)
	{
		//Look up the transition
		FSM transition = LookUpTable(FSM_MODE,*(inStr.nPos), &outStr);
		if(transition != LOOKUP_ERR)
		{
			FSM_MODE = transition;
			inStr.nPos++;
		}
		else
		{
			printf("\nFatal lookup error! State: %s, Char: %c, Location: %d\n",EnumToString(FSM_MODE),*(inStr.nPos),inStr.nPos-inStr.oPos);
			break;
		}
	}
	printf("\n");
	outStr.PrintString();
	printf("\n");
	system("pause");
	return 0;
}