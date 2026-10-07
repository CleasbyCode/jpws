# jpws

Embed a raw ***PowerShell*** script within a ***JPG*** image to create a tweetable ***JPG-PowerShell*** polyglot file.

![Demo Image](https://github.com/CleasbyCode/jpws/blob/main/demo_image/jpws_59c8d64131c8e.jpg)

**Credits:**
* Image "Rainbow Dragon" — [Duncan Crombie / @theartofweb](https://x.com/theartofweb)
* PowerShell "text-sine.ps1" — [Darren Shaw / @gierrofo](https://x.com/gierrofo)

There is a [***Web edition***](https://cleasbycode.co.uk/jpws/app/) of ***jpws***, which you can use immediately, as a convenient alternative to downloading and compiling the CLI source code.

An experimental ***Rust*** port [***jpws-rs***](https://github.com/CleasbyCode/jpws-rs) is also available for those interested in that language.  

## Usage (***Linux***)

```console

$ sudo apt install libturbojpeg0-dev libjpeg-dev

$ chmod +x compile_jpws.sh
$ ./compile_jpws.sh

$ Compilation successful. Executable 'jpws' created.
$ sudo cp jpws /usr/bin

$ jpws

Usage: jpws [-alt] <cover_image> <pwsh_script>
       jpws --info

$ jpws dragon.jpg text-sine.ps1

Checking cover image for comment-block close sequences "#>" (0x23, 0x3E).

Image will be progressively recompressed first; dimensions will only be reduced if needed.

Recompress:-

Chroma:  4:4:4 (fast)
Quality: 90%
Width:   900
Height:  604

Saved JPG-PowerShell polyglot image: jpws_e00c002f41d86.jpg (272720 bytes).

Comment-block close sequences successfully removed from image.

Please check to make sure size & quality of cover image is acceptable.

Complete!
```
https://github.com/user-attachments/assets/a096bc4b-79ab-41e4-af9a-50c5a222aee6

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

The important part is the final ***"3C 23" ("<#")***.
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

The cover image must not contain any ***"#>" (0x23, 0x3E)*** byte sequence, apart from the ***jpws*** required sequences.  

If the cover image contains a close-comment "#>" sequence, ***PowerShell*** will close the comment too early and then try to execute ***JPG*** bytes and the script will fail.

***jpws*** checks for these sequences and modifies the cover image when needed.

The current process is:

1. Validate the input ***JPG***.
2. Apply ***EXIF*** orientation if needed.
3. Strip/canonicalize leading metadata.
4. Convert the cover image to progressive ***JPG***.
5. Replace the leading header with the clean ***JFIF*** layout ***jpws*** expects.
6. Search the resulting ***JPG*** bytes for ***"#>"***.

If no ***"#>"*** sequence remains, the image can be used.

If the byte sequence ***"#>"*** is still present, ***jpws*** first tries same-dimension recompression.  
It uses progressive ***4:4:4 JPG*** only, trying these ***DCT*** variants:

  ***4:4:4 fast***  
  ***4:4:4 accurate***

Quality starts at 97 and decreases down to 75. At quality 96 and above, only the ***4:4:4 accurate*** variant is used, because ***libjpeg-turbo*** uses accurate ***DCT*** at those high quality levels even when fast ***DCT*** is requested.

If same-dimension recompression still cannot remove the ***"#>"*** byte sequences, ***jpws*** tries resizing.    

Each resize attempt reduces the shorter dimension by one more pixel, scaling the other dimension proportionally, up to 300 attempts.  
Resize encoding also uses progressive ***4:4:4*** only, with the same fast/accurate variants.  

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

If one of those appears, ***jpws*** prints a warning but still keeps the tail-patched image as-is.  

It no longer re-encodes the cover image in response to a tail warning: re-encoding was found to reduce ***X-Twitter*** compatibility, because the re-encoded compressed data was less likely to preserve the final ***"#>"*** through ***X-Twitter's*** processing.  

Once the cover image is comment-block free, it is left untouched.

***jpws*** treats this warning as expected:

  ***premature end of data segment***

That warning is normal for many successful tail-patched images.  

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

Always click the posted image to fully expand it before saving. Otherwise you may download a resized, payload-free variant instead of the original-size image.

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

The easiest way to download the image from ***X-Twitter*** and run the embedded ***PowerShell*** script is to use ***wget*** for Linux and ***iwr*** for Windows.
**Make sure ***PowerShell*** is installed on your Linux PC.**

You will first need to get the image link address from ***X-Twitter***, after you have posted the image.

Click the image in the post to fully expand it, then ***right-click*** on the image and select "***Copy image address***" from the menu.  

You can then paste the image address as part of the ***wget*** or ***iwr*** command, for example:

Linux:
```console
$ wget -O game.jpg "https://pbs.twimg.com/media/GhZTR8BXgAACc9Q?format=jpg&name=medium";pwsh game.jpg <script_arguments>
```

Windows:
```console
G:\demo> iwr -OutFile Game.ps1 "https://pbs.twimg.com/media/GhZTR8BXgAACc9Q?format=jpg&name=medium";.\Game.ps1 <script_arguments>
```

Alternatively, just manually save/download the image from ***X-Twitter*** (Click image within the post to fully expand it before saving).

To run the script embedded within the image using Linux, just enter the following command within a terminal.

```console
$ pwsh your_downloaded_image_name.jpg <script_arguments>
```
For Windows, after downloading the image from ***X-Twitter***, you will need to rename the ***.jpg*** file extension to ***.ps1***, also, depending on the Windows/PowerShell execution policy,
you will probably need to unblock the file before you can run the embedded script. 

```console
G:\demo> ren your_downloaded_image_name.jpg your_downloaded_image_name.ps1
G:\demo> Unblock-File your_downloaded_image_name.ps1 
G:\demo> powershell (or pwsh) -ExecutionPolicy Bypass -File .\your_downloaded_image_name.ps1 <script_arguments>
```

Matrix Rain demo image.
```
$ pwsh matrix_rain.jpg -Music
```

![Demo Image2](https://github.com/CleasbyCode/jpws/blob/main/demo_image/matrix_rain.jpg)  


![Demo Image3](https://github.com/CleasbyCode/jpws/blob/main/demo_image/game_of_life.jpg)  

## Limits

Current limits enforced by the program:  

 Cover image extension: .jpg, .jpeg, or .jfif  
 Cover image size: maximum 5 MB  
 Script extension: .ps1  
 Script size: maximum about 10 KB  
 
 ***PowerShell*** scripts that use a top "script-level" ***param(...) block*** do ***not work reliably*** when embedded within an image.  
 
 Depending on the script, the embedded ***param*** block is either ignored (the script still runs, but its named parameters do not bind) or it stops the script from running at all.  
 
 This is because the embedded script runs after a leading ***"cls;"***, so the ***param*** block is no longer the first statement, and only comments or blank lines (and sometimes a #requires statement) are allowed before it.  
 
 ***jpws*** detects a leading ***param*** block and prints a warning, but still generates the image. To read runtime switches, use ***$args*** instead, e.g. ***$Music = $args -contains '-Music'***.  
 
 A ***param*** block inside a function, rather than at the top of the script, is fine.  
  
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
Checking local ***JPG*** warnings and reporting (without re-encoding) when the tail shape looks unsafe.  

The method is inherently dependent on ***X-Twitter*** preserving a small patched tail.  
The default and ***"-alt"*** option tails are both workarounds for that black-box behavior.

## Third-Party Libraries

This project makes use of the following third-party libraries:

[stb_image](https://github.com/nothings/stb) by Sean Barrett (“nothings”)

libjpeg-turbo (see [***LICENSE***](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/LICENSE.md) file)  

{This software is based in part on the work of the Independent JPEG Group.}

