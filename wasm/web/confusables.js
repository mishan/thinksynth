/*
 * Copyright (C) 2004-2026 Metaphonic Labs
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General
 * Public License along with this program; if not, write to the
 * Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * confusables.js -- written by scripts/make-confusables.mjs from Unicode's
 * confusables.txt, version 18.0.0 (2026-08-06); do not edit.
 *
 * Each character that passes for Latin letters or digits, and what it
 * passes for: hex code point, then its skeleton.
 *
 * The notice above is this project's, for the code. The table is derived
 * from Unicode's data, which is Unicode's, and is distributed under its
 * own license:
 *
 * UNICODE LICENSE V3
 *
 * COPYRIGHT AND PERMISSION NOTICE
 *
 * Copyright © 1991-2026 Unicode, Inc.
 *
 * NOTICE TO USER: Carefully read the following legal agreement. BY
 * DOWNLOADING, INSTALLING, COPYING OR OTHERWISE USING DATA FILES, AND/OR
 * SOFTWARE, YOU UNEQUIVOCALLY ACCEPT, AND AGREE TO BE BOUND BY, ALL OF THE
 * TERMS AND CONDITIONS OF THIS AGREEMENT. IF YOU DO NOT AGREE, DO NOT
 * DOWNLOAD, INSTALL, COPY, DISTRIBUTE OR USE THE DATA FILES OR SOFTWARE.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of data files and any associated documentation (the "Data Files") or
 * software and any associated documentation (the "Software") to deal in the
 * Data Files or Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, and/or sell
 * copies of the Data Files or Software, and to permit persons to whom the
 * Data Files or Software are furnished to do so, provided that either (a)
 * this copyright and permission notice appear with all copies of the Data
 * Files or Software, or (b) this copyright and permission notice appear in
 * associated Documentation.
 *
 * THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
 * KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
 * THIRD PARTY RIGHTS.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS NOTICE
 * BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES,
 * OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
 * ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THE DATA
 * FILES OR SOFTWARE.
 *
 * Except as contained in this notice, the name of a copyright holder shall
 * not be used in advertising or otherwise to promote the sale, use or other
 * dealings in these Data Files or Software without prior written
 * authorization of the copyright holder.
 */

export const SKELETON = new Map(`
30=O 31=l 49=l 69=l 6d=rn 7c=l a1=i a2=c a5=Y c6=AE d0=D d7=x d8=O
e6=ae f8=o fe=p 110=D 111=d 126=H 127=h 131=i 132=lJ 133=ij 141=L 142=l
14b=n 152=OE 153=oe 166=T 167=t 17f=f 180=b 182=b 183=b 184=b 189=D
18b=d 18c=d 18d=g 191=F 192=f 196=l 197=l 199=k 19a=l 19d=N 19e=n 19f=O
1a5=p 1a6=R 1a7=2 1ab=t 1ad=t 1ae=T 1b4=y 1b5=Z 1b6=z 1b7=3 1bb=2 1bc=5
1bd=s 1bf=p 1c0=l 1c1=ll 1c4=DZ 1c5=Dz 1c6=dz 1c7=LJ 1c8=Lj 1c9=lj
1ca=NJ 1cb=Nj 1cc=nj 1e4=G 1e5=g 1f1=DZ 1f2=Dz 1f3=dz 21c=3 222=8 223=8
224=Z 225=z 237=j 23c=c 23e=T 244=U 246=E 247=e 248=J 249=j 24c=R 24d=r
24e=Y 24f=y 251=a 253=b 256=d 257=d 25f=j 260=g 261=g 263=y 266=h 268=i
269=i 26a=i 26b=l 26d=l 26f=w 271=rn 272=n 273=n 275=o 27c=r 27d=r
282=s 284=f 28b=u 28f=y 290=z 2a0=q 2a3=dz 2a6=ts 2a9=fn 2aa=ls 2ab=lz
37f=J 391=A 392=B 395=E 396=Z 397=H 398=O 399=l 39a=K 39c=M 39d=N 39f=O
3a1=P 3a4=T 3a5=Y 3a7=X 3b1=a 3b3=y 3b7=n 3b8=O 3b9=i 3bd=v 3bf=o 3c1=p
3c3=o 3c5=u 3d1=O 3d2=Y 3dc=F 3e8=2 3ec=6 3ed=o 3f1=p 3f2=c 3f3=j 3f4=O
3f8=p 3f9=C 3fa=M 405=S 406=l 408=J 410=A 411=b 412=B 415=E 417=3 41a=K
41c=M 41d=H 41e=O 420=P 421=C 422=T 423=Y 425=X 42b=bl 42c=b 42e=lO
430=a 431=6 433=r 435=e 43e=o 440=p 441=c 443=y 445=x 448=w 455=s 456=i
458=j 45b=h 45f=u 461=w 462=b 463=b 472=O 473=o 474=V 475=v 478=Oy
479=oy 47d=w 48c=b 48d=b 493=r 498=3 49a=K 49e=K 4a2=H 4aa=C 4ab=c
4ac=T 4ae=Y 4af=y 4b0=Y 4b1=y 4b2=X 4ba=h 4bb=h 4bd=e 4bf=e 4c0=l 4c7=H
4c9=H 4cd=M 4cf=l 4d4=AE 4d5=ae 4e0=3 4e8=O 4e9=o 501=d 50c=G 51a=Q
51b=q 51c=W 51d=w 545=3 54d=U 54f=S 555=O 560=rn 561=w 563=q 566=q
570=h 572=n 575=j 578=n 57c=n 57d=u 581=g 582=i 584=f 585=o 5c0=l 5d5=l
5d8=v 5df=l 5e1=o 5f0=ll 627=l 629=o 647=o 661=l 665=o 667=V 673=l
6be=o 6c1=o 6c3=o 6d5=o 6f1=l 6f5=o 6f7=V 6ff=o 7c0=O 7ca=l 7cb=o 7cc=Y
7d3=F 7d5=b 7e0=T 840=o 964=l 965=ll 966=o 969=3 9e6=o 9ea=8 9ed=9
a66=o a67=9 a6a=8 ae6=o ae9=3 b03=8 b20=O b66=o b68=9 be6=o c02=o c66=o
c82=o ce6=O d02=o d1f=s d20=o d66=o d6d=9 d82=o e50=o ed0=o 1004=c
1010=o 101d=o 1040=o 104a=l 104b=ll 105a=c 10b9=h 10bd=S 10c7=2 10cd=Z
10d7=o 10e7=y 10fd=S 10ff=o 110b=o 1147=oo 11bc=o 11ee=oo 1200=U 12d0=O
1340=O 13a0=D 13a1=R 13a2=T 13a4=O 13a5=i 13a9=Y 13aa=A 13ab=J 13ac=E
13b3=W 13b7=M 13bb=H 13bd=Y 13be=O 13bf=t 13c0=G 13c2=h 13c3=Z 13cc=U
13ce=4 13cf=b 13d2=R 13d4=W 13d5=S 13d9=V 13da=S 13de=L 13df=C 13e2=P
13e6=K 13e7=d 13eb=O 13ee=6 13f2=h 13f3=G 13f4=B 142f=V 144c=U 146d=P
146f=d 1472=b 1473=b 148d=J 14aa=L 14bf=2 1541=x 157c=H 157d=x 1587=R
15af=b 15b4=F 15c5=A 15de=D 15ea=D 15f0=M 15f7=B 1616=2 166d=X 166e=x
16b7=X 16c1=l 16c5=l 16d0=l 16d5=K 16d6=M 1702=3 172a=7 1763=x 17e0=o
1a32=o 1a45=o 1a80=o 1a90=o 1bea=o 1bec=x 1c82=o 1c83=c 1c84=7 1c95=3
1cb7=2 1cbd=S 1cbf=O 1cf5=X 1d04=c 1d0f=o 1d11=o 1d1c=u 1d20=v 1d21=w
1d22=z 1d26=r 1d6b=ue 1d6e=f 1d6f=rn 1d70=n 1d72=r 1d74=s 1d75=t 1d76=z
1d7b=i 1d7c=i 1d7d=p 1d7e=u 1d83=g 1d8c=y 1e9d=f 1efa=lL 1eff=y 2016=ll
20a1=C 20a5=rn 20a8=Rs 20a9=W 20ab=d 20ad=K 20ae=T 20b6=lt 2102=C
210a=g 210b=H 210c=H 210d=H 210e=h 210f=h 2110=l 2111=l 2112=L 2113=l
2115=N 2116=No 2119=P 211a=Q 211b=R 211c=R 211d=R 2121=TEL 2124=Z
2128=Z 212c=B 212d=C 212e=e 212f=e 2130=E 2131=F 2133=M 2134=o 2139=i
213b=FAX 213d=y 2145=D 2146=d 2147=e 2148=i 2149=j 2160=l 2161=ll
2162=lll 2163=lV 2164=V 2165=Vl 2166=Vll 2167=Vlll 2168=lX 2169=X
216a=Xl 216b=Xll 216c=L 216d=C 216e=D 216f=M 2170=i 2171=ii 2172=iii
2173=iv 2174=v 2175=vi 2176=vii 2177=viii 2178=ix 2179=x 217a=xi
217b=xii 217c=l 217d=c 217e=d 217f=rn 21bf=l 2205=O 221e=oo 2223=l
2225=ll 2228=v 222a=U 2296=O 229d=O 22a4=T 22c1=v 22c3=U 22ff=E 2300=O
2361=T 2365=O 236c=O 2373=i 2374=p 2376=a 2378=i 237a=a 23fd=l 2502=l
2503=l 2573=X 27d9=T 292b=x 292c=x 29e2=w 2a2f=x 2a30=x 2c67=H 2c69=K
2c6b=Z 2c6c=z 2c82=B 2c85=r 2c8c=Z 2c8d=z 2c8e=H 2c90=O 2c91=o 2c92=l
2c93=i 2c94=K 2c98=M 2c9a=N 2c9c=3 2c9e=O 2c9f=o 2ca2=P 2ca3=p 2ca4=C
2ca5=c 2ca6=T 2ca8=Y 2ca9=y 2cac=X 2cbd=w 2cc4=3 2cca=9 2ccb=9 2ccc=3
2cce=P 2ccf=p 2cd0=L 2cd2=6 2cd3=6 2cdc=6 2d2d=z 2d31=O 2d38=V 2d39=E
2d41=O 2d4a=l 2d4f=l 2d54=O 2d55=Q 2d5d=X 3007=O 3112=T 311a=Y 3147=o
3180=oo 3250=PTE 32cc=Hg 32cd=erg 32ce=eV 32cf=LTD 3371=hPa 3372=da
3373=AU 3374=bar 3375=oV 3376=pc 3377=drn 337a=lU 3380=pA 3381=nA
3383=rnA 3384=kA 3385=KB 3386=MB 3387=GB 3388=cal 3389=kcal 338a=pF
338b=nF 338e=rng 338f=kg 3390=Hz 3391=kHz 3392=MHz 3393=GHz 3394=THz
3396=rnl 3397=dl 3398=kl 3399=frn 339a=nrn 339c=rnrn 339d=crn 339e=krn
33a9=Pa 33aa=kPa 33ab=MPa 33ac=GPa 33ad=rad 33b0=ps 33b1=ns 33b3=rns
33b4=pV 33b5=nV 33b7=rnV 33b8=kV 33b9=MV 33ba=pW 33bb=nW 33bd=rnW
33be=kW 33bf=MW 33c3=Bq 33c4=cc 33c5=cd 33c8=dB 33c9=Gy 33ca=ha 33cb=HP
33cc=in 33cd=KK 33ce=KM 33cf=kt 33d0=lrn 33d1=ln 33d2=log 33d3=lx
33d4=rnb 33d5=rnil 33d6=rnol 33d7=PH 33d9=PPM 33da=PR 33db=sr 33dc=Sv
33dd=Wb 33ff=gal 4e05=T 4e2b=Y a4d0=B a4d1=P a4d2=d a4d3=D a4d4=T
a4d6=G a4d7=K a4d9=J a4da=C a4dc=Z a4dd=F a4df=M a4e0=N a4e1=L a4e2=S
a4e3=R a4e6=V a4e7=H a4ea=W a4eb=X a4ec=Y a4ee=A a4f0=E a4f2=l a4f3=O
a4f4=U a50b=T a516=lll a543=6 a557=B a56f=l a576=S a589=8 a5cb=E a644=2
a647=i a68c=T a695=h a698=OO a699=oo a6a2=o a6b2=Y a6c9=Z a6df=V a6ef=2
a728=T3 a731=s a732=AA a733=aa a734=AO a735=ao a736=AU a737=au a738=AV
a739=av a73a=AV a73b=av a73c=AY a73d=ay a740=K a74a=O a74b=o a74e=OO
a74f=oo a75a=2 a761=w a76a=3 a76e=9 a76f=9 a777=tf a781=l a798=F a799=f
a79f=u a7ab=3 a7ae=l a7b2=J a7b3=X a7b4=B a7c5=S a7fa=w a7fe=l a830=l
a8ce=l a8cf=ll a8f6=3 aa5d=l ab32=e ab35=f ab3d=o ab3e=o ab43=co
ab44=co ab47=r ab48=r ab4e=u ab51=rn ab52=u ab5a=y ab63=uo ab64=a
ab74=o ab75=i ab81=r ab83=w ab8e=o ab93=z ab9c=u ab9e=4 aba4=w aba9=v
abaa=s abaf=c abbb=o abbe=6 fb00=ff fb01=fi fb02=fl fb03=ffi fb04=ffl
fb05=ft fb06=st fba4=o fba5=o fba6=o fba7=o fba8=o fba9=o fbaa=o fbab=o
fbac=o fbad=o fcd9=o fd3c=l fd3d=l fe31=l fe81=l fe82=l fe87=l fe88=l
fe8d=l fe8e=l fe93=o fe94=o fee9=o feea=o feeb=o feec=o ff10=O ff11=l
ff12=2 ff13=3 ff14=4 ff15=5 ff16=6 ff17=7 ff18=8 ff19=9 ff21=A ff22=B
ff23=C ff24=D ff25=E ff26=F ff27=G ff28=H ff29=l ff2a=J ff2b=K ff2c=L
ff2d=M ff2e=N ff2f=O ff30=P ff31=Q ff32=R ff33=S ff34=T ff35=U ff36=V
ff37=W ff38=X ff39=Y ff3a=Z ff41=a ff42=b ff43=c ff44=d ff45=e ff46=f
ff47=g ff48=h ff49=i ff4a=j ff4b=k ff4c=l ff4d=rn ff4e=n ff4f=o ff50=p
ff51=q ff52=r ff53=s ff54=t ff55=u ff56=v ff57=w ff58=x ff59=y ff5a=z
ff5c=l ffb7=o ffe0=c ffe5=Y ffe6=W ffe8=l 1017e=f 1018b=d 1018e=N
10196=X 10197=V 10198=llS 10199=ll 10282=B 10286=E 10287=F 1028a=l
10290=X 10292=O 10295=P 10296=S 10297=T 102a0=A 102a1=B 102a2=C 102a5=F
102ab=O 102b0=M 102b1=T 102b2=Y 102b4=X 102cf=H 102f5=Z 10301=B 10302=C
10309=l 1030f=O 10311=M 10315=T 10317=X 1031a=8 1031c=b 10320=l 10322=X
10404=O 10415=C 1041b=L 10420=S 1042c=o 1043d=c 10448=s 104b4=R 104c2=O
104ce=U 104d2=7 104ea=o 104f6=u 10507=Z 1050e=l 10513=N 10516=O 10518=K
1051b=C 1051d=V 1051e=O 10525=F 10526=L 10527=X 10926=l 1092c=o 10c13=X
10c17=O 10c1f=V 10c20=Y 10c21=M 10c3e=l 10c82=X 10ca5=l 10cc2=x 10cfa=l
10cfc=X 10d07=o 11047=l 11048=ll 110c0=l 110c1=ll 11116=o 11124=o
11141=l 11142=ll 111c5=l 111c6=ll 11302=o 113d4=l 113d5=ll 11445=8
1144b=l 1144c=ll 114c5=w 114d0=o 115c5=l 11641=l 11642=ll 11700=rn
11706=v 1170a=w 1170e=w 1170f=w 118a0=V 118a2=F 118a3=L 118a4=Y 118a6=E
118a9=Z 118ac=9 118ae=E 118af=4 118b2=L 118b5=O 118b8=U 118bb=5 118bc=T
118c0=v 118c1=s 118c2=F 118c3=i 118c4=y 118c6=7 118c8=o 118ca=3 118cc=9
118d5=6 118d6=9 118d7=o 118d8=u 118dc=y 118e0=O 118e3=rn 118e5=Z
118e6=W 118e9=C 118ec=X 118ef=W 118f2=C 11abc=Z 11abe=N 11c41=l
11c42=ll 11dda=l 11de0=O 11de1=l 1699b=O 169c1=9 169fe=8 16a19=r
16ad6=S 16ae4=l 16ae9=O 16d63=l 16e80=O 16e82=4 16e8a=7 16eaa=l 16eb6=b
16f08=V 16f0a=T 16f16=L 16f28=l 16f35=R 16f3a=S 16f3b=3 16f40=A 16f42=U
16f43=Y 1ccd6=A 1ccd7=B 1ccd8=C 1ccd9=D 1ccda=E 1ccdb=F 1ccdc=G 1ccdd=H
1ccde=l 1ccdf=J 1cce0=K 1cce1=L 1cce2=M 1cce3=N 1cce4=O 1cce5=P 1cce6=Q
1cce7=R 1cce8=S 1cce9=T 1ccea=U 1cceb=V 1ccec=W 1cced=X 1ccee=Y 1ccef=Z
1ccf0=O 1ccf1=l 1ccf2=2 1ccf3=3 1ccf4=4 1ccf5=5 1ccf6=6 1ccf7=7 1ccf8=8
1ccf9=9 1cefc=V 1d100=l 1d134=c 1d1fe=7 1d206=3 1d207=b 1d20c=W 1d20d=V
1d212=7 1d213=F 1d216=R 1d21a=O 1d22a=L 1d262=ll 1d373=T 1d377=l
1d400=A 1d401=B 1d402=C 1d403=D 1d404=E 1d405=F 1d406=G 1d407=H 1d408=l
1d409=J 1d40a=K 1d40b=L 1d40c=M 1d40d=N 1d40e=O 1d40f=P 1d410=Q 1d411=R
1d412=S 1d413=T 1d414=U 1d415=V 1d416=W 1d417=X 1d418=Y 1d419=Z 1d41a=a
1d41b=b 1d41c=c 1d41d=d 1d41e=e 1d41f=f 1d420=g 1d421=h 1d422=i 1d423=j
1d424=k 1d425=l 1d426=rn 1d427=n 1d428=o 1d429=p 1d42a=q 1d42b=r
1d42c=s 1d42d=t 1d42e=u 1d42f=v 1d430=w 1d431=x 1d432=y 1d433=z 1d434=A
1d435=B 1d436=C 1d437=D 1d438=E 1d439=F 1d43a=G 1d43b=H 1d43c=l 1d43d=J
1d43e=K 1d43f=L 1d440=M 1d441=N 1d442=O 1d443=P 1d444=Q 1d445=R 1d446=S
1d447=T 1d448=U 1d449=V 1d44a=W 1d44b=X 1d44c=Y 1d44d=Z 1d44e=a 1d44f=b
1d450=c 1d451=d 1d452=e 1d453=f 1d454=g 1d456=i 1d457=j 1d458=k 1d459=l
1d45a=rn 1d45b=n 1d45c=o 1d45d=p 1d45e=q 1d45f=r 1d460=s 1d461=t
1d462=u 1d463=v 1d464=w 1d465=x 1d466=y 1d467=z 1d468=A 1d469=B 1d46a=C
1d46b=D 1d46c=E 1d46d=F 1d46e=G 1d46f=H 1d470=l 1d471=J 1d472=K 1d473=L
1d474=M 1d475=N 1d476=O 1d477=P 1d478=Q 1d479=R 1d47a=S 1d47b=T 1d47c=U
1d47d=V 1d47e=W 1d47f=X 1d480=Y 1d481=Z 1d482=a 1d483=b 1d484=c 1d485=d
1d486=e 1d487=f 1d488=g 1d489=h 1d48a=i 1d48b=j 1d48c=k 1d48d=l
1d48e=rn 1d48f=n 1d490=o 1d491=p 1d492=q 1d493=r 1d494=s 1d495=t
1d496=u 1d497=v 1d498=w 1d499=x 1d49a=y 1d49b=z 1d49c=A 1d49e=C 1d49f=D
1d4a2=G 1d4a5=J 1d4a6=K 1d4a9=N 1d4aa=O 1d4ab=P 1d4ac=Q 1d4ae=S 1d4af=T
1d4b0=U 1d4b1=V 1d4b2=W 1d4b3=X 1d4b4=Y 1d4b5=Z 1d4b6=a 1d4b7=b 1d4b8=c
1d4b9=d 1d4bb=f 1d4bd=h 1d4be=i 1d4bf=j 1d4c0=k 1d4c1=l 1d4c2=rn
1d4c3=n 1d4c5=p 1d4c6=q 1d4c7=r 1d4c8=s 1d4c9=t 1d4ca=u 1d4cb=v 1d4cc=w
1d4cd=x 1d4ce=y 1d4cf=z 1d4d0=A 1d4d1=B 1d4d2=C 1d4d3=D 1d4d4=E 1d4d5=F
1d4d6=G 1d4d7=H 1d4d8=l 1d4d9=J 1d4da=K 1d4db=L 1d4dc=M 1d4dd=N 1d4de=O
1d4df=P 1d4e0=Q 1d4e1=R 1d4e2=S 1d4e3=T 1d4e4=U 1d4e5=V 1d4e6=W 1d4e7=X
1d4e8=Y 1d4e9=Z 1d4ea=a 1d4eb=b 1d4ec=c 1d4ed=d 1d4ee=e 1d4ef=f 1d4f0=g
1d4f1=h 1d4f2=i 1d4f3=j 1d4f4=k 1d4f5=l 1d4f6=rn 1d4f7=n 1d4f8=o
1d4f9=p 1d4fa=q 1d4fb=r 1d4fc=s 1d4fd=t 1d4fe=u 1d4ff=v 1d500=w 1d501=x
1d502=y 1d503=z 1d504=A 1d505=B 1d507=D 1d508=E 1d509=F 1d50a=G 1d50d=J
1d50e=K 1d50f=L 1d510=M 1d511=N 1d512=O 1d513=P 1d514=Q 1d516=S 1d517=T
1d518=U 1d519=V 1d51a=W 1d51b=X 1d51c=Y 1d51e=a 1d51f=b 1d520=c 1d521=d
1d522=e 1d523=f 1d524=g 1d525=h 1d526=i 1d527=j 1d528=k 1d529=l
1d52a=rn 1d52b=n 1d52c=o 1d52d=p 1d52e=q 1d52f=r 1d530=s 1d531=t
1d532=u 1d533=v 1d534=w 1d535=x 1d536=y 1d537=z 1d538=A 1d539=B 1d53b=D
1d53c=E 1d53d=F 1d53e=G 1d540=l 1d541=J 1d542=K 1d543=L 1d544=M 1d546=O
1d54a=S 1d54b=T 1d54c=U 1d54d=V 1d54e=W 1d54f=X 1d550=Y 1d552=a 1d553=b
1d554=c 1d555=d 1d556=e 1d557=f 1d558=g 1d559=h 1d55a=i 1d55b=j 1d55c=k
1d55d=l 1d55e=rn 1d55f=n 1d560=o 1d561=p 1d562=q 1d563=r 1d564=s
1d565=t 1d566=u 1d567=v 1d568=w 1d569=x 1d56a=y 1d56b=z 1d56c=A 1d56d=B
1d56e=C 1d56f=D 1d570=E 1d571=F 1d572=G 1d573=H 1d574=l 1d575=J 1d576=K
1d577=L 1d578=M 1d579=N 1d57a=O 1d57b=P 1d57c=Q 1d57d=R 1d57e=S 1d57f=T
1d580=U 1d581=V 1d582=W 1d583=X 1d584=Y 1d585=Z 1d586=a 1d587=b 1d588=c
1d589=d 1d58a=e 1d58b=f 1d58c=g 1d58d=h 1d58e=i 1d58f=j 1d590=k 1d591=l
1d592=rn 1d593=n 1d594=o 1d595=p 1d596=q 1d597=r 1d598=s 1d599=t
1d59a=u 1d59b=v 1d59c=w 1d59d=x 1d59e=y 1d59f=z 1d5a0=A 1d5a1=B 1d5a2=C
1d5a3=D 1d5a4=E 1d5a5=F 1d5a6=G 1d5a7=H 1d5a8=l 1d5a9=J 1d5aa=K 1d5ab=L
1d5ac=M 1d5ad=N 1d5ae=O 1d5af=P 1d5b0=Q 1d5b1=R 1d5b2=S 1d5b3=T 1d5b4=U
1d5b5=V 1d5b6=W 1d5b7=X 1d5b8=Y 1d5b9=Z 1d5ba=a 1d5bb=b 1d5bc=c 1d5bd=d
1d5be=e 1d5bf=f 1d5c0=g 1d5c1=h 1d5c2=i 1d5c3=j 1d5c4=k 1d5c5=l
1d5c6=rn 1d5c7=n 1d5c8=o 1d5c9=p 1d5ca=q 1d5cb=r 1d5cc=s 1d5cd=t
1d5ce=u 1d5cf=v 1d5d0=w 1d5d1=x 1d5d2=y 1d5d3=z 1d5d4=A 1d5d5=B 1d5d6=C
1d5d7=D 1d5d8=E 1d5d9=F 1d5da=G 1d5db=H 1d5dc=l 1d5dd=J 1d5de=K 1d5df=L
1d5e0=M 1d5e1=N 1d5e2=O 1d5e3=P 1d5e4=Q 1d5e5=R 1d5e6=S 1d5e7=T 1d5e8=U
1d5e9=V 1d5ea=W 1d5eb=X 1d5ec=Y 1d5ed=Z 1d5ee=a 1d5ef=b 1d5f0=c 1d5f1=d
1d5f2=e 1d5f3=f 1d5f4=g 1d5f5=h 1d5f6=i 1d5f7=j 1d5f8=k 1d5f9=l
1d5fa=rn 1d5fb=n 1d5fc=o 1d5fd=p 1d5fe=q 1d5ff=r 1d600=s 1d601=t
1d602=u 1d603=v 1d604=w 1d605=x 1d606=y 1d607=z 1d608=A 1d609=B 1d60a=C
1d60b=D 1d60c=E 1d60d=F 1d60e=G 1d60f=H 1d610=l 1d611=J 1d612=K 1d613=L
1d614=M 1d615=N 1d616=O 1d617=P 1d618=Q 1d619=R 1d61a=S 1d61b=T 1d61c=U
1d61d=V 1d61e=W 1d61f=X 1d620=Y 1d621=Z 1d622=a 1d623=b 1d624=c 1d625=d
1d626=e 1d627=f 1d628=g 1d629=h 1d62a=i 1d62b=j 1d62c=k 1d62d=l
1d62e=rn 1d62f=n 1d630=o 1d631=p 1d632=q 1d633=r 1d634=s 1d635=t
1d636=u 1d637=v 1d638=w 1d639=x 1d63a=y 1d63b=z 1d63c=A 1d63d=B 1d63e=C
1d63f=D 1d640=E 1d641=F 1d642=G 1d643=H 1d644=l 1d645=J 1d646=K 1d647=L
1d648=M 1d649=N 1d64a=O 1d64b=P 1d64c=Q 1d64d=R 1d64e=S 1d64f=T 1d650=U
1d651=V 1d652=W 1d653=X 1d654=Y 1d655=Z 1d656=a 1d657=b 1d658=c 1d659=d
1d65a=e 1d65b=f 1d65c=g 1d65d=h 1d65e=i 1d65f=j 1d660=k 1d661=l
1d662=rn 1d663=n 1d664=o 1d665=p 1d666=q 1d667=r 1d668=s 1d669=t
1d66a=u 1d66b=v 1d66c=w 1d66d=x 1d66e=y 1d66f=z 1d670=A 1d671=B 1d672=C
1d673=D 1d674=E 1d675=F 1d676=G 1d677=H 1d678=l 1d679=J 1d67a=K 1d67b=L
1d67c=M 1d67d=N 1d67e=O 1d67f=P 1d680=Q 1d681=R 1d682=S 1d683=T 1d684=U
1d685=V 1d686=W 1d687=X 1d688=Y 1d689=Z 1d68a=a 1d68b=b 1d68c=c 1d68d=d
1d68e=e 1d68f=f 1d690=g 1d691=h 1d692=i 1d693=j 1d694=k 1d695=l
1d696=rn 1d697=n 1d698=o 1d699=p 1d69a=q 1d69b=r 1d69c=s 1d69d=t
1d69e=u 1d69f=v 1d6a0=w 1d6a1=x 1d6a2=y 1d6a3=z 1d6a4=i 1d6a5=j 1d6a8=A
1d6a9=B 1d6ac=E 1d6ad=Z 1d6ae=H 1d6af=O 1d6b0=l 1d6b1=K 1d6b3=M 1d6b4=N
1d6b6=O 1d6b8=P 1d6b9=O 1d6bb=T 1d6bc=Y 1d6be=X 1d6c2=a 1d6c4=y 1d6c8=n
1d6c9=O 1d6ca=i 1d6ce=v 1d6d0=o 1d6d2=p 1d6d4=o 1d6d6=u 1d6dd=O 1d6e0=p
1d6e2=A 1d6e3=B 1d6e6=E 1d6e7=Z 1d6e8=H 1d6e9=O 1d6ea=l 1d6eb=K 1d6ed=M
1d6ee=N 1d6f0=O 1d6f2=P 1d6f3=O 1d6f5=T 1d6f6=Y 1d6f8=X 1d6fc=a 1d6fe=y
1d702=n 1d703=O 1d704=i 1d708=v 1d70a=o 1d70c=p 1d70e=o 1d710=u 1d717=O
1d71a=p 1d71c=A 1d71d=B 1d720=E 1d721=Z 1d722=H 1d723=O 1d724=l 1d725=K
1d727=M 1d728=N 1d72a=O 1d72c=P 1d72d=O 1d72f=T 1d730=Y 1d732=X 1d736=a
1d738=y 1d73c=n 1d73d=O 1d73e=i 1d742=v 1d744=o 1d746=p 1d748=o 1d74a=u
1d751=O 1d754=p 1d756=A 1d757=B 1d75a=E 1d75b=Z 1d75c=H 1d75d=O 1d75e=l
1d75f=K 1d761=M 1d762=N 1d764=O 1d766=P 1d767=O 1d769=T 1d76a=Y 1d76c=X
1d770=a 1d772=y 1d776=n 1d777=O 1d778=i 1d77c=v 1d77e=o 1d780=p 1d782=o
1d784=u 1d78b=O 1d78e=p 1d790=A 1d791=B 1d794=E 1d795=Z 1d796=H 1d797=O
1d798=l 1d799=K 1d79b=M 1d79c=N 1d79e=O 1d7a0=P 1d7a1=O 1d7a3=T 1d7a4=Y
1d7a6=X 1d7aa=a 1d7ac=y 1d7b0=n 1d7b1=O 1d7b2=i 1d7b6=v 1d7b8=o 1d7ba=p
1d7bc=o 1d7be=u 1d7c5=O 1d7c8=p 1d7ca=F 1d7ce=O 1d7cf=l 1d7d0=2 1d7d1=3
1d7d2=4 1d7d3=5 1d7d4=6 1d7d5=7 1d7d6=8 1d7d7=9 1d7d8=O 1d7d9=l 1d7da=2
1d7db=3 1d7dc=4 1d7dd=5 1d7de=6 1d7df=7 1d7e0=8 1d7e1=9 1d7e2=O 1d7e3=l
1d7e4=2 1d7e5=3 1d7e6=4 1d7e7=5 1d7e8=6 1d7e9=7 1d7ea=8 1d7eb=9 1d7ec=O
1d7ed=l 1d7ee=2 1d7ef=3 1d7f0=4 1d7f1=5 1d7f2=6 1d7f3=7 1d7f4=8 1d7f5=9
1d7f6=O 1d7f7=l 1d7f8=2 1d7f9=3 1d7fa=4 1d7fb=5 1d7fc=6 1d7fd=7 1d7fe=8
1d7ff=9 1df24=tO 1df2d=d 1df2e=dz 1df31=y 1df32=h 1df34=q 1df37=r
1df39=u 1df3c=O 1df3f=w 1df40=A 1df41=a 1df45=g 1df46=h 1df47=h 1df48=K
1df49=k 1df4a=M 1df4b=rn 1df4c=rn 1df4d=N 1df4e=n 1df4f=n 1df51=V
1df52=v 1df55=y 1df5a=a 1df5d=ie 1df5e=oi 1df5f=ou 1df64=th 1df65=wh
1df6a=A 1df6e=l 1df7d=w 1df81=E 1e140=O 1e141=l 1e145=V 1e2f0=O 1e2f2=9
1e8c7=l 1e8cb=8 1ed01=l 1ee00=l 1ee24=o 1ee64=ol 1ee80=l 1ee84=o
1f16d=cc 1f16e=C 1f190=DJ 1f700=QE 1f707=AR 1f708=V 1f714=O 1f74c=C
1f75c=sss 1f768=T 1f76b=MB 1f76c=VB 1fbf0=O 1fbf1=l 1fbf2=2 1fbf3=3
1fbf4=4 1fbf5=5 1fbf6=6 1fbf7=7 1fbf8=8 1fbf9=9
`.trim().split(/\s+/).map((e) =>
{
    const [hex, to] = e.split('=');

    return [String.fromCodePoint(parseInt(hex, 16)), to];
}));
