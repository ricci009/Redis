# Redis

#PACKAGES#
boost

#TODO#
learn .md so I can make a nice read me lol

#COMMANDS#
compile server $g++ -Wall -Wextra -g main.cpp network.cpp -o server
compile client $g++ -Wall -Wextra -g client.cpp -o client

#CLIENT PROTOCOL#
|len|ms1|len|msg2|len|msg3|more...
