# Dungeon-Game---CS-327
A Fun Dungeon game coded in C and C++ using Ncurses and several algorithms. The Game was developed through-out a semester as part of a CS course.

-------------------------------------------------------------------------------------------------------------------------
Make note that you will need to add the object_description and monster_description files in a folder named .rlg327   for this game to work.

-------------------------------------------------------------------------------------------------------------------------
Hello!

This is the assignment 1.10 for cs 327 Iowa State University.

I decided to add some more features to the dungeon game.

I used --MY-- code from assignment 1.09( built on Professor Shaeffer's 1.08 code) as I found this approach easier. 

****I HAVE ATTACHED THE NEW OBJECT DESC AND MONSTER DESC IN CANVAS. PLEASE MAKE SURE TO USE THEM IN THE ./rlg327 FOLDER INSTEAD OF THE PREVIOUS ONE**** 

New features include:-

(1) Food- I have added lobsters, swordfish, and shark objects of FOOD type to the game. they are random on the dungeon like anyother object.
	  They can be picked up. and when the user enters the 'w' key, and selects one of the Food, it heals the PC.
	  
(2) gp (other name for money) - I have added a concept of money for the PC. PC can find 'PileOfGold' in the dungeon/ from random games / from killing monsters - ie monster drops. 
				the PC originally has 0 gp. 'PileOfGold' is an object of GOLD type. when picked up, the value of it(which can vary) is added to the PC's money pouch.
				
(3) stats - The player can press 'S' to see the stats of the PC. this includes name, speed, damage, hitpoints, and the money in the PC's money pouch.

(4) Mini-Game - Mini-Games are CONTAINER objects and can be randomly found in the dungeon. the function the same way as food or potions. it can be picked up, and when 
		worn(press 'w'), there is a  40% chance to add 1000 hitpoints to the PC, 40% change to add 500gp to the Pc's money pouch, and a 20% chance to half the life of the PC.
	  
(5) Vendor- Press 'v' key to open the shop- it sells potions and the ShaefferGodsword. They cost money depends on the VALUE of the object 
	 
(6) Potions- Speed potions and Damage potions can be bought from the vendor. speed potion adds 100 to the PC's speed. this lasts for 40 turns only. Damage potion does the same, but adds 
	      100 to damage of PC. Both cost 1001 gp. Potions do not appear anywhere else in the game. They can only be bought from the vendor.
	      
(7) ShaefferGodsword - This is an IMPORTANT update. if the PC wears any weapon other than the ShaefferGodsword while fighting a monster with a BOSS tag, it will always hit 0.
			To fight and kill a boss, the PC must be weilding the ShaefferGodsword. It is not found anywhere in the game. It can only be bought from the vendor for 2501gp.
			
(8) Monster Drops- When killed, monsters now drop "PileOfGold"(look at gp) in the monsters position. The gp value of the this drop varies based on the Hitpoints and Damage of the monster killed. 
			
Other Changes-

The Monsterdescription file has been changed to make monsters stronger(have more HP, damage, and speed) to make the game challenging.
The object description file has also been changed. weapons have new damage, speed etc. and new objects have been added.

****I HAVE ATTACHED THE NEW OBJECT DESC AND MONSTER DESC IN CANVAS. PLEASE MAKE SURE TO USE THEM IN THE ./rlg327 FOLDER INSTEAD OF THE PREVIOUS ONE**** 
	
