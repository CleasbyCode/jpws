# jpws

A Linux CLI tool that embeds a PowerShell script in a JPG image, creating a JPG–PowerShell polyglot intended for sharing on X/Twitter. The file can be displayed as an image and run as a script. Survival through X/Twitter's image processing must be checked after downloading it again.

![Rainbow Dragon demo](https://github.com/CleasbyCode/jpws/blob/main/demo_image/dragon.jpg)

**Credits:** “Rainbow Dragon” image by [Duncan Crombie](https://x.com/theartofweb); original `text-sine.ps1` script by [Darren Shaw](https://x.com/gierrofo).

```sh
pwsh ./dragon.jpg -Music
```

Related projects: [Web edition](https://cleasbycode.co.uk/jpws/app/) and experimental [Rust port, jpws-rs](https://github.com/CleasbyCode/jpws-rs).

## Build on Linux

Requirements:

- Bash and GCC 14 or newer, including its C++ standard library. The supplied build script invokes `g++` and uses C++23.
- TurboJPEG and libjpeg development packages.
- PowerShell 7 (`pwsh`) to run embedded scripts. PowerShell is not required to build or run the `jpws` encoder.

On Debian/Ubuntu systems whose repositories provide GCC 14 or newer:

```sh
sudo apt install g++ libturbojpeg0-dev libjpeg-dev
```

Check `g++ --version` before building. Installing these packages on a distribution with an older default compiler does not satisfy the compiler requirement.

Run these commands from this `src` directory:

```sh
g++ --version
bash compile_jpws.sh
```

Successful build output:

```text
Compilation successful. Executable 'jpws' created.
```

Use `./jpws` directly, or optionally install it on your PATH:

```sh
sudo install -m 755 jpws /usr/local/bin/jpws
```

For a build optimized for the machine doing the compilation:

```sh
JPWS_NATIVE=1 bash compile_jpws.sh
```

This enables `-march=native` and, where available, the resize library's FMA path. Use the default build when the executable needs to run on other CPUs.

## Usage

```text
jpws [-alt] <cover_image> <pwsh_script>
jpws --info
```

For example, from `src`, with your cover saved as `cover.jpg`:

```sh
./jpws cover.jpg pwsh_scripts/text-sine.ps1
./jpws --info
```

The output is a new `jpws_<random-suffix>.jpg` in the current working directory. Input files are not overwritten. Output files are created with owner-only read/write permissions, subject to the process umask.

An example progress block is:

```text
Image Status: Recompress

Chroma Subsampling [4:4:4] Mode: Fast

Quality: 90%

Dimensions: 900x604
```

`Image Status` changes to `Resize` when dimensions are being reduced. On a terminal, the block updates in place; redirected output contains occasional plain-text blocks without terminal control sequences. Dimensions, quality, filename, and output size depend on the input.

On success, the program reports the saved filename and byte count. Inspect the resulting image before posting it.

Demo videos:

https://github.com/user-attachments/assets/cd8521b9-154b-4b30-aa78-6aa65f7228a1

https://github.com/user-attachments/assets/16652459-fa31-4247-833e-0d8516fdd400

## Input and output limits

| Item | Enforced limit or requirement |
| --- | --- |
| Cover extension | `.jpg`, `.jpeg`, or `.jfif`, case-insensitive |
| Cover file size | 134–5,242,880 bytes |
| Completed output size | At most 5 MiB (5,242,880 bytes), including the embedded profile |
| Cover dimensions | 400–8,192 pixels on each axis |
| Cover pixel count | At most 25,000,000 pixels |
| JPEG colour space | YCbCr or grayscale; other colour spaces, including CMYK, are rejected |
| Script extension | `.ps1`, case-insensitive |
| Script file size | At most 9,812 bytes, including any UTF-8 BOM |
| Script payload size | At least 10 bytes after removing any UTF-8 BOM |
| Input file type | Regular files; symbolic links in the final path component are rejected |

Use UTF-8 scripts, with or without a BOM. A leading UTF-8 BOM is removed before embedding. BOM-marked UTF-16 and UTF-32 files are rejected; the program does not comprehensively validate the remaining text as UTF-8 or check PowerShell syntax.

Spaces and parentheses in filenames are supported; quote those paths in shell commands. Control characters in the filename itself are rejected.

Scripts with a top-level `param(...)` block do not work reliably after embedding because the template puts `cls;` before the script. `jpws` warns when it recognizes a leading `param` block but still generates the image; this is not a full PowerShell parser. Read runtime switches from `$args` instead, for example:

```powershell
$Music = $args -contains '-Music'
```

A `param` block inside a function is unaffected by this placement restriction. Other script dependencies and compatibility requirements still apply to the machine running the image.

## How it works

PowerShell block comments use `<#` and `#>`. The polyglot places the JPEG bytes inside comments while exposing the embedded script between them. JPEG decoders ignore or tolerate the profile payload and patched tail; local validation checks the final file before it is saved.

### File layout

1. **Opening comment in JFIF APP0.** At byte offset `0x0C`, `jpws` writes `58 54 57 0A 3C 23`, representing `XTW\n<#`. The opening `<#` comments out the following header and profile bytes.
2. **Script in APP2/ICC.** A profile segment follows the 20-byte JFIF header. Its template contains `#>cls;`, followed by the script, then `\r\n<#\r\n` to comment out the remaining JPEG data.
3. **Closing comment in the final scan.** Ten bytes of compressed data immediately before the EOI marker (`FF D9`) are replaced with one of the following tails:

| Mode | Tail bytes |
| --- | --- |
| Default | `9E 23 3E 0D 23 00 00 20 20 00` |
| `-alt` | `00 20 20 00 00 23 3E 0D 23 9E` |

`23 3E` is `#>`. The following `0D 23` starts a new line and a PowerShell line comment for the remaining bytes. The tail is placed in scan data because previous upload tests found that extra post-scan COM and APP2-style segments were stripped.

![JFIF layout](https://github.com/CleasbyCode/jpws/blob/main/demo_image/first_block.png)

![Image before upload](https://github.com/CleasbyCode/jpws/blob/main/demo_image/before_tweet.png)

### Cover processing

Every input is canonicalized, even if it initially contains no `#>`:

1. Validate the JPEG header, colour space, dimensions, and pixel count.
2. Apply EXIF orientation, discard metadata, and produce a progressive JPEG. Where a lossless coefficient transform cannot preserve the complete edges, decode and orient pixels instead.
3. Replace the leading metadata with the expected clean JFIF header.
4. Check that the JPEG contains no `#>` and fits the output-size allowance after reserving space for the embedded profile.

If either candidate check fails, try same-dimension progressive 4:4:4 recompression at qualities 97 down to 75. At qualities 97 and 96, only accurate DCT is tried; below that, fast and accurate DCT are tried. This gives up to 44 candidates. The initial coefficient transform can preserve the input's subsampling; not every accepted output is necessarily 4:4:4.

If needed, try up to 300 resized candidates. Attempt `n` reduces the original shorter axis by `n` pixels and scales the other axis proportionally, rounded to the nearest pixel. Each attempt resizes from the retained source pixels rather than the preceding candidate. Quality starts at 97 and decreases by two every 15 attempts. Each resize attempt uses one of the available DCT modes and progressive 4:4:4 encoding. Neither dimension may fall below 400 pixels.

#### Optional processing limit

There is **no default pixel-work budget**: normal runs have the full candidate search available, subject to the attempt and dimension limits above.

For automated processing, an explicit limit can be set:

```sh
JPWS_MAX_PIXEL_WORK=250000000 ./jpws cover.jpg pwsh_scripts/text-sine.ps1
```

Each pixel encode charges its pixel count. Each resize additionally charges its source and destination pixel counts. If the configured budget is exhausted, the program exits without saving output. Increase or unset `JPWS_MAX_PIXEL_WORK` to allow more work; `JPWS_MAX_PIXEL_WORK=0` disables this optional limit. This is a limit on repeated encode/resize work, not a wall-clock timeout.

### Final validation

Once a candidate is comment-free and fits the size allowance, `jpws` assembles the full output once in memory. Before patching, it checks that all ten tail bytes fit inside the final scan after its last header or restart marker, without splitting a stuffed byte pair.

A cover with insufficient or unsuitable final scan data is rejected. This can happen with otherwise valid JPEGs, including solid-colour images. Try another cover rather than expecting the program to overwrite a scan header.

After patching the tail and JFIF comment bytes, libjpeg decodes the complete output:

- **Fatal decoder errors reject the output. No file is saved.**
- Recoverable decoder warnings are reported, but the image is retained. These include extraneous data, premature EOF, and other warnings such as a premature end of a data segment.

Recoverable warnings do not trigger another encode. Previous upload experiments found that warning-driven re-encoding could reduce tail survival. Local decoding is not a simulation of X/Twitter, and neither a warning-free decode nor a tolerated warning guarantees that an upload will preserve the script.

## Sharing on X/Twitter

The project's upload observations include preservation of the early JFIF comment and first APP2/ICC profile in tested images, removal of extra post-scan metadata segments, and changes to final scan bytes. Some progressive JPEGs were not fully re-encoded. These are observations, not guarantees; the exact platform processing is unknown and may change. No dated upload-test matrix is maintained in this README.

To check a particular result:

1. Generate the output and inspect it locally.
2. Post it to X/Twitter.
3. Expand the posted image before saving it or copying its image address.
4. Obtain the original-size variant where available, rather than a preview or a URL explicitly requesting `name=medium`.
5. Check the downloaded image, embedded payload, and comment boundaries, then test the intended script on the downloaded file.

Expanding the image helps avoid a preview, but does not prove that the downloaded file is original or that its payload survived. Verify the result after download.

Use the default mode first. If the downloaded tail does not work, try the other layout:

```sh
./jpws -alt cover.jpg pwsh_scripts/text-sine.ps1
```

If both modes fail, change the cover, crop or scale it, or re-save it with different compression before trying again. Repeating identical inputs and settings with the same build normally produces identical image bytes; only the random output filename changes. Reposting those bytes may test platform variability, but does not create a new local encoding.

## Downloading and running an embedded script

The examples below use PowerShell 7 (`pwsh`). The CLI tests can check syntax with the locally installed `pwsh`; they do not establish compatibility with every PowerShell release or Windows PowerShell 5.1. See Microsoft's [PowerShell command-line documentation](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_pwsh) for platform-specific file handling.

Use the address of the intended original-size image obtained through the procedure above. Downloading and execution are separate steps: proceed to execution only after the download succeeds and you have checked the resulting file. Append actual script arguments after the filename when needed; do not type angle-bracket placeholders.

### Linux: Bash and PowerShell 7

Download using `wget` in Bash:

```bash
read -r -p 'Original-size image URL: ' image_url
wget -O downloaded.jpg "$image_url"
```

After a successful download and verification, run it:

```sh
pwsh -File ./downloaded.jpg
```

PowerShell on Linux can run this file with its `.jpg` extension. For a script that supports the music switch, append `-Music`.

### Windows: PowerShell 7

Run these commands in a PowerShell session, not Command Prompt. Download directly to a `.ps1` filename:

```powershell
$imageUrl = Read-Host 'Original-size image URL'
Invoke-WebRequest -Uri $imageUrl -OutFile .\downloaded.ps1 -ErrorAction Stop
```

Alternatively, if you saved the file manually as `downloaded.jpg`, rename it:

```powershell
Rename-Item -LiteralPath .\downloaded.jpg -NewName downloaded.ps1
```

After a successful download and verification, unblock the file if it carries a downloaded-file mark, then run it:

```powershell
Unblock-File -LiteralPath .\downloaded.ps1
pwsh -File .\downloaded.ps1
```

On Windows, `pwsh -File` requires the `.ps1` extension. Execution remains subject to the machine's execution policy; unblocking removes the downloaded-file mark but does not change that policy. Script-specific dependencies may also differ between Linux and Windows.

### Published demos

These commands describe the published demo images; their embedded scripts may differ from the files in `pwsh_scripts`.

Matrix Rain (`-Music` enables its Linux music option):

```sh
pwsh ./matrix_rain.jpg -Music
```

![Matrix Rain demo](https://github.com/CleasbyCode/jpws/blob/main/demo_image/matrix_rain.jpg)

Conway's Game of Life (arguments: board width, board height, step count, and optional `-Sound`):

```sh
pwsh ./game_of_life.jpg 132 33 500 -Sound
```

![Game of Life demo](https://github.com/CleasbyCode/jpws/blob/main/demo_image/game_of_life.jpg)

## Tests

Run from `src`:

```sh
bash tests/run_unit_tests.sh
bash tests/run_cli_tests.sh
bash tests/run_golden_tests.sh
```

All three suites require the build dependencies and standard Linux shell utilities. The CLI and golden suites also require Python 3; the CLI suite uses `mkfifo` and `timeout`, and the golden suite requires `ffmpeg`. Installing `pwsh` enables the CLI suite's optional PowerShell parser checks; those checks do not execute the embedded scripts.

- Unit tests build with AddressSanitizer and UndefinedBehaviorSanitizer, covering bounds, orientation, profile lengths, optional work limits, and tail safety. If LeakSanitizer is unavailable in the environment, the runner retries with leak detection disabled.
- CLI tests cover argument/file handling, script-size boundaries, profile layout, and redirected progress output.
- Golden tests check both stored fixtures and freshly generated outputs using structural checks and independent JPEG decoders.

The scripts build current sources in temporary directories by default. `JPWS` can select a binary for CLI tests, and `JPWS_BIN` can select one for golden integration tests. `bash tests/run_golden_tests.sh --verify-only` checks stored golden fixtures without generating new outputs, but still builds the independent JPEG checker.

These suites test local behaviour; they do not upload images or establish current X/Twitter compatibility.

## Third-party libraries

- [`stb_image_resize2`](https://github.com/nothings/stb), by Jeff Roberts (v2) and Jorge L Rodriguez, from the stb collection. The bundled header is version 2.16; see [the bundled license](stb_image/LICENSE.md).
- [libjpeg-turbo](https://github.com/libjpeg-turbo/libjpeg-turbo), providing the TurboJPEG and libjpeg APIs; see its [license](https://github.com/libjpeg-turbo/libjpeg-turbo/blob/main/LICENSE.md).

This software is based in part on the work of the Independent JPEG Group.
