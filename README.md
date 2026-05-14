# jpws

Embed a ***PowerShell*** script within a ***JPG*** image to create a tweetable ***JPG-PowerShell*** polyglot file.

![Demo Image](https://github.com/CleasbyCode/jpws/blob/main/demo_image/jpws_27965.jpg)
***Credits:
{Image "Rainbow Dragon" [Duncan Crombie / @theartofweb](https://x.com/theartofweb)
PowerShell "text-sine.ps1" [Darren Shaw / @gierrofo](https://x.com/gierrofo)}***

## Usage (***Linux***)

```console

user1@linuxbox:~/Downloads/src$ sudo apt install libturbojpeg0-dev
user1@linuxbox:~/Downloads/src$ chmod +x compile_jpws.sh
user1@linuxbox:~/Downloads/src$ ./compile_jpws.sh
user1@linuxbox:~/Downloads/src$ Compilation successful. Executable 'jpws' created.
user1@linuxbox:~/Downloads/src$ sudo cp jpws /usr/bin
user1@linuxbox:~/Desktop$ jpws

Usage: jpws [-alt] <cover_image> <pwsh_script>
       jpws --info

user1@linuxbox:~/Desktop$ jpws dragon.jpg sinewave.ps1

Saved "PowerShell-embedded" JPG image: jpws_10247.jpg (121098 bytes).

Complete!
```
https://github.com/user-attachments/assets/f5b87dbf-885e-4cb5-a70c-5879c82f7e20

## How It Works

*Note: When downloading images from ***X-Twitter***, always click the image in the post to ***FULLY EXPAND*** it before saving. This ensures you get the original size image with the embedded payload.*

***jpws*** creates a ***JPG-PowerShell*** polyglot: the same file is still a displayable ***JPG*** image, but ***PowerShell*** can also parse it as a script.
The trick is ***PowerShell*** block comments. ***PowerShell*** ignores everything between:

  ***<#***

and:

  ***#>***

***JPG*** decoders ignore or tolerate the non-image data that ***jpws*** uses for the ***PowerShell*** payload.  

The ***PowerShell*** script works only if the comment boundaries survive the round trip through ***X-Twitter***.

## High-Level Layout  

(1) Opening ***PowerShell*** comment block

***jpws*** writes an opening ***"<#"*** sequence into the ***JFIF APP0*** area near the beginning of the file.
The current bytes written at the ***JFIF*** comment-block location are:

  ***58 54 57 0A 3C 23***

That is:

  ***XTW\n<#***

The important part is the final ***"3e 23" ("<#")***.
This makes ***PowerShell*** treat the following ***JPG*** header/profile bytes as comment text instead of executable code.

***X-Twitter*** preserves this early ***JFIF*** area.

![JFIF Image](https://github.com/CleasbyCode/jpws/blob/main/demo_image/first_block.png)  

(2) ***PowerShell*** script inside the ***APP2/ICC*** profile

The ***PowerShell*** payload is inserted into an ***APP2 ICC*** profile segment.  
***X-Twitter*** preserves this first ***APP2/ICC*** segment, including the embedded script.

The profile template contains a close-comment sequence before the script:

  ***#>cls;***

That closes the initial block comment and begins executable ***PowerShell***.  
The user's script is inserted after that.

After the script, the profile template opens another block comment:

  ***<#***

That comments out the rest of the ***JPG*** bytes until ***jpws*** supplies the final close-comment sequence near the end of the file.  

(3) Final close-comment tail

***PowerShell*** requires the final block comment to be closed. Therefore ***jpws*** must place a final "#>" near the end of the ***JPG***.

This is the fragile part.

***jpws*** currently overwrites the last 10 bytes of compressed image data before the ***JPG*** EOI marker (FF D9).  

It does not insert an extra ***JPG*** marker segment for the tail, because ***X-Twitter*** strips post-scan ***COM*** segments and a second post-scan ***APP2/ICC-style*** segment.

The default tail bytes are currently:

  ***9E 23 3E 0D 23 00 00 20 20 00***

The ***"-alt"*** option tail bytes are currently:

  ***00 20 20 00 00 23 3E 0D 23 9E***

These byte strings are written immediately before:

  ***FF D9***

The important bytes are:

  ***23 3E***

which is ***"#>"***.

The following:

  ***0D 23***

is intentional. It starts a new line and then a ***PowerShell*** line comment, so if
extra bytes after the close marker survive, they are more likely to be ignored by ***PowerShell***.

![BEFORE Image](https://github.com/CleasbyCode/jpws/blob/main/demo_image/before_tweet.png)

## Cover Image Compatibility

The cover image must not contain any ***"#>" (0x23, 0x3C)*** byte sequence, apart from the ***jpws*** required sequences. If the cover image contains an
close-comment "#>" sequence, ***PowerShell*** will close the comment too early and then try to execute ***JPG*** bytes and the script will fail.

***jpws*** checks for these sequences and modifies the cover image when needed.

The current process is:

1. Validate the input ***JPG***.
2. Apply ***EXIF*** orientation if needed.
3. Strip/canonicalize leading metadata.
4. Convert the cover image to progressive ***JPG***.
5. Replace the leading header with the clean ***JFIF*** layout ***jpws*** expects.
6. Search the resulting **JPG** bytes for ***"#>"***.

If no ***"#>"*** sequence remains, the image can be used.

If the byte sequence ***"#>"*** is still present, ***jpws*** first tries same-dimension recompression.  
It uses progressive ***4:4:4 JPG*** only, trying these ***DCT*** variants:

  ***4:4:4 default***  
  ***4:4:4 accurate***  
  ***4:4:4 fast***

Quality starts at 97 and decreases down to 75.

If same-dimension recompression still cannot remove the ***"#>"*** byte sequences, ***jpws*** tries resizing.    

Each resize attempt reduces both width and height by one more pixel, up to 300 attempts.  
Resize encoding also uses progressive ***4:4:4*** only, with the same default/accurate/fast variants.  

Quality is reduced by 2 every 15 resize attempts.

The image dimensions must stay at least 400x400 pixels.

## Tail Validation

After the cover image is compatible, ***jpws*** patches the final tail bytes and then builds the full polyglot image in memory.

It then asks ***libjpeg*** to decode the full output and records warnings.

The warning check is only a local heuristic. It is not a perfect ***X-Twitter*** simulation.

***jpws*** treats these as unsafe:

  ***extraneous bytes before marker 0xD9***
  ***premature EOF***
  ***fatal JPEG decode errors***

If one of those appears, ***jpws*** retries by generating another progressive ***4:4:4*** cover-image candidate, patching the tail again, and checking warnings again.

***jpws*** currently allows:

  ***premature end of data segment***

That warning is expected for many successful tail-patched images.  

Outputs with ***"premature end of data segment"*** have been more likely to survive ***X-Twitter*** than outputs with ***"extraneous bytes before marker 0xD9"***.

This is not a guarantee. It is just the best local signal found so far.

## What X-Twitter Appears To Do

***X-Twitter's*** exact ***JPG*** processing is not known.

Current observations:

* Progressive ***JPGs*** within size and dimension limits are often not fully re-encoded.
* The early ***JFIF*** area containing the opening ***"<#"*** is preserved in tests.
* The first ***APP2/ICC*** profile segment containing the ***PowerShell*** script is preserved in tests.
* Extra ***COM*** marker segments near the end of the ***JPG*** are stripped.
* A second ***APP2/ICC-style*** segment near the end was also stripped.
* ***X-Twitter*** may rewrite or normalize the final compressed entropy bytes near ***EOI***. Often the final ***"#>"*** survives, but sometimes it does not.
* Some failures modify only the final tail area; other images may be processed differently.

Because of this, ***jpws*** cannot currently predict success with certainty. The only reliable test is still:

1. Create the ***jpws*** output.
2. Post it to ***X-Twitter***.
3. Open/expand the posted image.
4. Download the expanded image.
5. Check whether the final ***"#>"*** tail correctly survived and whether the ***PowerShell*** script still runs.

Always click the posted image to fully expand it before saving. Otherwise you may download a resized no payload variant instead of the original-size image.

## Default vs -alt

Use the default mode first:

  ***$ jpws cover_image.jpg script.ps1***

If the downloaded ***X-Twitter*** image no longer contains a correct / working final tail, try:

  ***$ jpws -alt cover_image.jpg script.ps1***

The two modes place the final ***"#>"*** in different positions within the last 10 bytes before ***EOI***.  
Some images that fail with one tail layout will work with the other.

If both fail, the practical options are:

  Try a different cover image.  
  Crop or scale the image externally and run ***jpws*** again.  
  Re-save the image through an editor, then run ***jpws*** again.  
  Generate several ***jpws*** outputs and test them through ***X-Twitter***.

## Executing Embedded PowerShell Script

The easiest way to download the image from ***X-Twitter*** and run the embedded ***PowerShell*** script, is to use ***wget*** for Linux and ***iwr*** for Windows.
**Make sure ***PowerShell*** is installed on your Linux PC.**

You will first need to get the image link address from ***X-Twitter***, after you have posted the image.

Click the image in the post to fully expand it, then ***right-click*** on the image and select "***Copy image address***" from the menu.  
You can then paste the image address as part of the ***wget*** or ***iwr*** command, for example: 

Linux:
```console
wget -O game.jpg "https://pbs.twimg.com/media/GhZTR8BXgAACc9Q?format=jpg&name=medium";pwsh game.jpg
```

Windows:
```console
iwr -OutFile Game.ps1 "https://pbs.twimg.com/media/GhZTR8BXgAACc9Q?format=jpg&name=medium";.\Game.ps1
```

Alternatively, just manually save/download the image from ***X-Twitter*** (Click image within the post to fully expand it before saving).

To run the script embedded within the image using Linux, just enter the following command within a terminal. 

```console
$ pwsh your_downloaded_image_name.jpg
```
For Windows, after downloading the image from ***X-Twitter***, you will need to rename the ***.jpg*** file extension to ***.ps1***, also, depending on the Windows/PowerShell execution policy,
you will probably need to unblock the file before you can run the embedded script. 

```console
G:\demo> ren your_downloaded_image_name.jpg your_downloaded_image_name.ps1
G:\demo> Unblock-File your_downloaded_image_name.ps1 
G:\demo> powershell (or pwsh) -ExecutionPolicy Bypass -File .\your_downloaded_image_name.ps1
```
https://github.com/user-attachments/assets/2e49ea69-7e33-4b43-bcba-cb0a9678a4f7

## Limits

Current limits enforced by the program:

  Cover image extension: .jpg, .jpeg, or .jfif
  Cover image size: maximum 5 MB
  Script extension: .ps1
  Script size: maximum about 10 KB  
  
  ***PowerShell*** scripts that use a top "script-level" ***param(...) block*** will ***not work*** when embedded within an image.
  The param block enforces strict parsing at the start of the script.  
  The only things allowed before the param block are comments or blank lines (and sometimes a #requires statement).
  Having certain binary bytes before param will break parsing. A param block inside a function, rather than at the top of the script, should work fine.  
  
  Cover image dimensions: at least 400x400 pixels
  Cover image dimensions: no more than 8192 pixels in either dimension
  Cover image pixels: no more than 25 megapixels

## Summary

***jpws*** works by:

  Opening a ***PowerShell*** block comment near the start of the ***JPG***.
  Storing the ***PowerShell*** script inside an ***APP2/ICC*** profile segment.
  Reopening a ***PowerShell*** block comment after the script.
  Closing that final block comment by patching a ***"#>"*** tail into the compressed image data immediately before ***FF D9***.
  Keeping all generated ***JPGs*** progressive.
  Recompressing/resizing the cover image with progressive ***4:4:4*** encoding until any ***"#>"*** byte sequences are removed.
  Checking local ***JPG*** warnings and retrying when the tail shape looks unsafe.

The method is inherently dependent on ***X-Twitter*** preserving a small patched tail.  
The default and ***"-alt"*** option tails are both workarounds for that black-box behavior.

## Third-Party Libraries

This project makes use of the following third-party libraries:

[stb_image](https://github.com/nothings/stb) by Sean Barrett (“nothings”)

libjpeg-turbo (see [***LICENSE***](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/LICENSE.md) file)
{This software is based in part on the work of the Independent JPEG Group.}
##

