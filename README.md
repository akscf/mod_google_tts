<p>
  The module helps to use Google cloud text-to-speech service in Freeswitch. <br>
  There is also used a simple caching that decrease response time and  reaction, but don't forget to clean it with a helper script.
</p>

### Dialplan example
[More](sources/conf/dialplan/example.xml)
```
<!--
     When the config option 'voice-name-as-language-code' is 'true', you can use it for define language
     Allow some short codes, such as: [en, de, es, is, ru]
     for other use BCP-47 (https://www.rfc-editor.org/rfc/bcp/bcp47.txt)
-->
<extension name="google-tts">
 <condition field="destination_number" expression="^(3111)$">
  <action application="answer"/>
  <action application="speak" data="google|en|Hello world!"/>
  <action application="hangup"/>
 </condition>
</extension>

<!--
    When 'voice-name-as-language-code' is 'false'
    it can be used for define voice type: male/female
-->
<extension name="google-tts">
 <condition field="destination_number" expression="^(3111)$">
  <action application="answer"/>
  <action application="speak" data="google|male|{lang=en}Hello world!"/>
  <action application="hangup"/>
 </condition>
</extension>

```
