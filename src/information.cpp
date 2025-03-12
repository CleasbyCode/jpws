void displayInfo() {
	std::cout << R"(

JPG-PowerShell Polyglot for X/Twitter (jpws v1.2) Created by Nicholas Cleasby (@CleasbyCode) 12/12/2024 

CLI tool for embedding a PowerShell script within a tweetable JPG image file.

Compile & run jpws (Linux):
		
$ sudo apt-get install libturbojpeg-dev
$ g++ main.cpp -O2 -lturbojpeg -s -o jpws
$ sudo cp jpws /usr/bin
$ jpws
		
Usage: jpws <cover_image> <powershell_script>
       jpws --info
		
Share your "PowerShell-embedded" JPG image on X/Twitter.

Max script size is ~10KB. 
Max image size is 4MB.

https://github.com/CleasbyCode/jpws

)";
}