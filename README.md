<p>
  Google Text-To-Speech service for the Freeswitch. <br>
</p>

### Dialplan example
```XML
<extension name="tts-test">
    <condition field="destination_number" expression="^(3333)$">
        <action application="answer"/>
        <action application="speak" data="google|en|Hello world!"/>
        <action application="sleep" data="1000"/>
        <action application="hangup"/>
    </condition>
</extension>
```

### mod_quickjs
```javascript
session.ttsEngine= 'google';
session.language = 'en';

session.speak('Hello world!');
```
