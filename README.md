<p>
  Freeswitch TTS module for the Google cloud Text-to-Speech service, based on v1-rest api. <br>
</p>

### Usage example
```xml
<extension name="tts-test">
    <condition field="destination_number" expression="^(3333)$">
	<action application="answer"/>
	<action application="speak" data="google|en|Hello world!"/>
        <action application="sleep" data="1000"/>
        <action application="hangup"/>
    </condition>
</extension>

```
